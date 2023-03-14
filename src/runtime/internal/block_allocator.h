#ifndef HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
#define HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

#include "HalideRuntime.h"
#include "linked_list.h"
#include "memory_resources.h"
#include "printer.h"
#include "region_allocator.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --

/** Allocator class interface for managing large contiguous blocks
 * of memory, which are then sub-allocated into smaller regions of
 * memory. This class only manages the address creation for the
 * regions -- allocation callback functions are used to request the
 * memory from the necessary system or API calls. This class is
 * intended to be used inside of a higher level memory management
 * class that provides thread safety, policy management and API
 * integration for a specific runtime API (eg Vulkan, OpenCL, etc)
 */
class BlockAllocator {
public:
    // disable copy constructors and assignment
    BlockAllocator(const BlockAllocator &) = delete;
    BlockAllocator &operator=(const BlockAllocator &) = delete;

    // disable non-factory based construction
    BlockAllocator() = delete;
    ~BlockAllocator() = delete;

    // Allocators for the different types of memory we need to allocate
    struct MemoryAllocators {
        SystemMemoryAllocatorFns system;
        MemoryBlockAllocatorFns block;
        MemoryRegionAllocatorFns region;
    };

    // Runtime configuration parameters to adjust the behaviour of the block allocator
    struct Config {
        size_t initial_capacity = 0;
        size_t minimum_block_size = 0;
        size_t maximum_block_size = 0;
        size_t maximum_block_count = 0;
    };

    // Factory methods for creation / destruction
    static BlockAllocator *create(void *user_context, const Config &config, const MemoryAllocators &allocators);
    static void destroy(void *user_context, BlockAllocator *block_allocator);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, const MemoryRequest &request);
    void reclaim(void *user_context, MemoryRegion *region);
    bool collect(void *user_context);  //< returns true if any blocks were removed
    void release(void *user_context);
    void destroy(void *user_context);

    // Access methods
    const MemoryAllocators &current_allocators() const;
    const Config &current_config() const;
    const Config &default_config() const;
    size_t block_count() const;

private:
    // Linked-list for storing the block resources
    typedef LinkedList::EntryType BlockEntry;

    // Initializes a new instance
    void initialize(void *user_context, const Config &config, const MemoryAllocators &allocators);

    // Reserves a region of memory using the given allocator for the given block resource, returns nullptr on failure
    MemoryRegion *reserve_memory_region(void *user_context, RegionAllocator *allocator, const MemoryRequest &request);

    // Creates a new region allocator for the given block resource
    RegionAllocator *create_region_allocator(void *user_context, BlockResource *block);

    // Destroys the given region allocator and all associated memory regions
    void destroy_region_allocator(void *user_context, RegionAllocator *region_allocator);

    // Reserves a block of memory for the requested size and returns the corresponding block entry, or nullptr on failure
    BlockEntry *reserve_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Locates the "best-fit" block entry for the requested size, or nullptr if none was found
    BlockEntry *find_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Creates a new block entry and int the list
    BlockEntry *create_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Releases the block entry from being used, and makes it available for further allocations
    void release_block_entry(void *user_context, BlockEntry *block_entry);

    // Destroys the block entry and removes it from the list
    void destroy_block_entry(void *user_context, BlockEntry *block_entry);

    // Invokes the allocation callback to allocate memory for the block region
    void alloc_memory_block(void *user_context, BlockResource *block);

    // Invokes the deallocation callback to free memory for the memory block
    void free_memory_block(void *user_context, BlockResource *block);

    // Returns a constrained size for the requested size based on config parameters
    size_t constrain_requested_size(size_t size) const;

    // Returns true if the given block is compatible with the given properties
    bool is_compatible_block(const BlockResource *block, const MemoryProperties &properties) const;

    Config config;
    LinkedList block_list;
    MemoryAllocators allocators;
};

BlockAllocator *BlockAllocator::create(void *user_context, const Config &cfg, const MemoryAllocators &allocators) {
    halide_debug_assert(user_context, allocators.system.allocate != nullptr);
    BlockAllocator *result = reinterpret_cast<BlockAllocator *>(
        allocators.system.allocate(user_context, sizeof(BlockAllocator)));

    if (result == nullptr) {
        error(user_context) << "BlockAllocator: Failed to create instance! Out of memory!\n";
        return nullptr;
    }

    result->initialize(user_context, cfg, allocators);
    return result;
}

void BlockAllocator::destroy(void *user_context, BlockAllocator *instance) {
    halide_debug_assert(user_context, instance != nullptr);
    const MemoryAllocators &allocators = instance->allocators;
    instance->destroy(user_context);
    halide_debug_assert(user_context, allocators.system.deallocate != nullptr);
    allocators.system.deallocate(user_context, instance);
}

void BlockAllocator::initialize(void *user_context, const Config &cfg, const MemoryAllocators &ma) {
    config = cfg;
    allocators = ma;
    block_list.initialize(user_context,
                          sizeof(BlockResource),
                          config.initial_capacity,
                          allocators.system);
}

MemoryRegion *BlockAllocator::reserve(void *user_context, const MemoryRequest &request) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Reserve ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "offset=" << (uint32_t)request.offset << " "
                        << "size=" << (uint32_t)request.size << " "
                        << "dedicated=" << (request.dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(request.properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(request.properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(request.properties.visibility) << ") ...\n";
#endif
    BlockEntry *block_entry = reserve_block_entry(user_context, request.properties, request.size, request.dedicated);
    if (block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new empty block of requested size ("
                            << (int32_t)(request.size) << " bytes)!\n";
        return nullptr;
    }

    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    halide_debug_assert(user_context, block != nullptr);
    halide_debug_assert(user_context, block->allocator != nullptr);

    MemoryRegion *result = reserve_memory_region(user_context, block->allocator, request);
    if (result == nullptr) {

        // Unable to reserve region in an existing block ... create a new block and try again.
        size_t actual_size = constrain_requested_size(request.size);
        block_entry = create_block_entry(user_context, request.properties, actual_size, request.dedicated);
        if (block_entry == nullptr) {
            debug(user_context) << "BlockAllocator: Out of memory! Failed to allocate empty block of size ("
                                << (int32_t)(actual_size) << " bytes)!\n";
            return nullptr;
        }

        block = static_cast<BlockResource *>(block_entry->value);
        if (block->allocator == nullptr) {
            block->allocator = create_region_allocator(user_context, block);
        }

        result = reserve_memory_region(user_context, block->allocator, request);
    }
    return result;
}

void BlockAllocator::reclaim(void *user_context, MemoryRegion *memory_region) {
    halide_debug_assert(user_context, memory_region != nullptr);
    RegionAllocator *allocator = RegionAllocator::find_allocator(user_context, memory_region);
    if (allocator == nullptr) {
        return;
    }
    allocator->reclaim(user_context, memory_region);
}

bool BlockAllocator::collect(void *user_context) {
    bool result = false;
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;

        const BlockResource *block = static_cast<BlockResource *>(block_entry->value);
        if (block->allocator == nullptr) {
            block_entry = prev_entry;
            continue;
        }

        block->allocator->collect(user_context);
        if (block->reserved == 0) {
            destroy_block_entry(user_context, block_entry);
            result = true;
        }

        block_entry = prev_entry;
    }
    return result;
}

void BlockAllocator::release(void *user_context) {
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;
        release_block_entry(user_context, block_entry);
        block_entry = prev_entry;
    }
}

void BlockAllocator::destroy(void *user_context) {
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;
        destroy_block_entry(user_context, block_entry);
        block_entry = prev_entry;
    }
    block_list.destroy(user_context);
}

MemoryRegion *BlockAllocator::reserve_memory_region(void *user_context, RegionAllocator *allocator, const MemoryRequest &request) {
    MemoryRegion *result = allocator->reserve(user_context, request);
    if (result == nullptr) {
#ifdef DEBUG_RUNTIME
        debug(user_context) << "BlockAllocator: Failed to allocate region of size ("
                            << (int32_t)(request.size) << " bytes)!\n";
#endif
        // allocator has enough free space, but not enough contiguous space
        // -- collect and try to reallocate
        if (allocator->collect(user_context)) {
            result = allocator->reserve(user_context, request);
        }
    }
    return result;
}

BlockAllocator::BlockEntry *
BlockAllocator::find_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
    BlockEntry *block_entry = nullptr;
    for (block_entry = block_list.front(); block_entry != nullptr; block_entry = block_entry->next_ptr) {

        const BlockResource *block = static_cast<BlockResource *>(block_entry->value);
        if (!is_compatible_block(block, properties)) {
            continue;
        }

        // skip blocks that can't be dedicated to a single allocation
        if (dedicated && (block->reserved > 0)) {
            continue;
        }

        // skip dedicated blocks that are already allocated
        if (block->memory.dedicated && (block->reserved > 0)) {
            continue;
        }

        size_t available = (block->memory.size - block->reserved);
        if (available >= size) {
#ifdef DEBUG_RUNTIME
            debug(user_context) << "BlockAllocator: find_block_entry (FOUND) ("
                                << "user_context=" << (void *)(user_context) << " "
                                << "block_entry=" << (void *)(block_entry) << " "
                                << "size=" << (uint32_t)size << " "
                                << "dedicated=" << (dedicated ? "true" : "false") << " "
                                << "usage=" << halide_memory_usage_name(properties.usage) << " "
                                << "caching=" << halide_memory_caching_name(properties.caching) << " "
                                << "visibility=" << halide_memory_visibility_name(properties.visibility) << ") ...\n";
#endif
            break;
        }
    }

    return block_entry;
}

BlockAllocator::BlockEntry *
BlockAllocator::reserve_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
    BlockEntry *block_entry = find_block_entry(user_context, properties, size, dedicated);
    if (block_entry == nullptr) {
        size_t actual_size = constrain_requested_size(size);
        block_entry = create_block_entry(user_context, properties, actual_size, dedicated);
    }

    if (block_entry) {
        BlockResource *block = static_cast<BlockResource *>(block_entry->value);
        if (block->allocator == nullptr) {
            block->allocator = create_region_allocator(user_context, block);
        }
    }
    return block_entry;
}

RegionAllocator *
BlockAllocator::create_region_allocator(void *user_context, BlockResource *block) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Creating region allocator ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_resource=" << (void *)(block) << ")...\n";
#endif
    halide_debug_assert(user_context, block != nullptr);
    RegionAllocator *region_allocator = RegionAllocator::create(
        user_context, block, {allocators.system, allocators.region});

    if (region_allocator == nullptr) {
        error(user_context) << "BlockAllocator: Failed to create new region allocator!\n";
        return nullptr;
    }

    return region_allocator;
}

void BlockAllocator::destroy_region_allocator(void *user_context, RegionAllocator *region_allocator) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Destroying region allocator ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "region_allocator=" << (void *)(region_allocator) << ")...\n";
#endif
    if (region_allocator == nullptr) {
        return;
    }
    RegionAllocator::destroy(user_context, region_allocator);
}

BlockAllocator::BlockEntry *
BlockAllocator::create_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
    if (config.maximum_block_count && (block_count() >= config.maximum_block_count)) {
        debug(user_context) << "BlockAllocator: No free blocks found! Maximum block count reached ("
                            << (int32_t)(config.maximum_block_count) << ")!\n";
        return nullptr;
    }

    BlockEntry *block_entry = block_list.append(user_context);
    if (block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new block entry!\n";
        return nullptr;
    }

#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Creating block entry ("
                        << "block_entry=" << (void *)(block_entry) << " "
                        << "block=" << (void *)(block_entry->value) << " "
                        << "allocator=" << (void *)(allocators.block.allocate) << ")...\n";
#endif

    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    block->memory.size = size;
    block->memory.properties = properties;
    block->memory.dedicated = dedicated;
    block->reserved = 0;
    block->allocator = create_region_allocator(user_context, block);
    alloc_memory_block(user_context, block);
    return block_entry;
}

void BlockAllocator::release_block_entry(void *user_context, BlockAllocator::BlockEntry *block_entry) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Releasing block entry ("
                        << "block_entry=" << (void *)(block_entry) << " "
                        << "block=" << (void *)(block_entry->value) << ")...\n";
#endif
    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    if (block->allocator) {
        block->allocator->release(user_context);
    }
}

void BlockAllocator::destroy_block_entry(void *user_context, BlockAllocator::BlockEntry *block_entry) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Destroying block entry ("
                        << "block_entry=" << (void *)(block_entry) << " "
                        << "block=" << (void *)(block_entry->value) << " "
                        << "deallocator=" << (void *)(allocators.block.deallocate) << ")...\n";
#endif
    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    if (block->allocator) {
        destroy_region_allocator(user_context, block->allocator);
        block->allocator = nullptr;
    }
    free_memory_block(user_context, block);
    block_list.remove(user_context, block_entry);
}

void BlockAllocator::alloc_memory_block(void *user_context, BlockResource *block) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Allocating block (ptr=" << (void *)block << " allocator=" << (void *)allocators.block.allocate << ")...\n";
#endif
    halide_debug_assert(user_context, allocators.block.allocate != nullptr);
    MemoryBlock *memory_block = &(block->memory);
    allocators.block.allocate(user_context, memory_block);
    block->reserved = 0;
}

void BlockAllocator::free_memory_block(void *user_context, BlockResource *block) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "BlockAllocator: Deallocating block (ptr=" << (void *)block << " allocator=" << (void *)allocators.block.deallocate << ")...\n";
#endif
    halide_debug_assert(user_context, allocators.block.deallocate != nullptr);
    MemoryBlock *memory_block = &(block->memory);
    allocators.block.deallocate(user_context, memory_block);
    block->reserved = 0;
    block->memory.size = 0;
}

size_t BlockAllocator::constrain_requested_size(size_t size) const {
    size_t actual_size = size;
    if (config.minimum_block_size) {
        actual_size = ((actual_size < config.minimum_block_size) ?
                           config.minimum_block_size :
                           actual_size);
    }
    if (config.maximum_block_size) {
        actual_size = ((actual_size > config.maximum_block_size) ?
                           config.maximum_block_size :
                           actual_size);
    }
    return actual_size;
}

bool BlockAllocator::is_compatible_block(const BlockResource *block, const MemoryProperties &properties) const {
    if (properties.caching != MemoryCaching::DefaultCaching) {
        if (properties.caching != block->memory.properties.caching) {
            return false;
        }
    }

    if (properties.visibility != MemoryVisibility::DefaultVisibility) {
        if (properties.visibility != block->memory.properties.visibility) {
            return false;
        }
    }

    if (properties.usage != MemoryUsage::DefaultUsage) {
        if (properties.usage != block->memory.properties.usage) {
            return false;
        }
    }

    return true;
}

const BlockAllocator::MemoryAllocators &BlockAllocator::current_allocators() const {
    return allocators;
}

const BlockAllocator::Config &BlockAllocator::current_config() const {
    return config;
}

const BlockAllocator::Config &BlockAllocator::default_config() const {
    static Config result;
    return result;
}

size_t BlockAllocator::block_count() const {
    return block_list.size();
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

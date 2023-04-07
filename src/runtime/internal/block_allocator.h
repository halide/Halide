#ifndef HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
#define HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

#include "../HalideRuntime.h"
#include "../printer.h"
#include "linked_list.h"
#include "memory_resources.h"
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
        size_t maximum_pool_size = 0;    //< Maximum number of bytes to allocate for the entire pool (including all blocks). Specified in bytes. Zero means no constraint
        size_t minimum_block_size = 0;   //< Minimum block size in bytes. Zero mean no constraint.
        size_t maximum_block_size = 0;   //< Maximum block size in bytes. Zero means no constraint
        size_t maximum_block_count = 0;  //< Maximum number of blocks to allocate. Zero means no constraint
        size_t nearest_multiple = 0;     //< Always round up the requested region sizes to the given integer value. Zero means no constraint
    };

    // Factory methods for creation / destruction
    static BlockAllocator *create(void *user_context, const Config &config, const MemoryAllocators &allocators);
    static void destroy(void *user_context, BlockAllocator *block_allocator);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, const MemoryRequest &request);
    int release(void *user_context, MemoryRegion *region);  //< unmark and cache the region for reuse
    int reclaim(void *user_context, MemoryRegion *region);  //< free the region and consolidate
    int retain(void *user_context, MemoryRegion *region);   //< retain the region and increase the usage count
    bool collect(void *user_context);                       //< returns true if any blocks were removed
    int release(void *user_context);
    int destroy(void *user_context);

    // Access methods
    const MemoryAllocators &current_allocators() const;
    const Config &current_config() const;
    const Config &default_config() const;
    size_t block_count() const;
    size_t pool_size() const;

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
    int destroy_region_allocator(void *user_context, RegionAllocator *region_allocator);

    // Reserves a block of memory for the requested size and returns the corresponding block entry, or nullptr on failure
    BlockEntry *reserve_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Locates the "best-fit" block entry for the requested size, or nullptr if none was found
    BlockEntry *find_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Creates a new block entry and int the list
    BlockEntry *create_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated);

    // Releases the block entry from being used, and makes it available for further allocations
    int release_block_entry(void *user_context, BlockEntry *block_entry);

    // Destroys the block entry and removes it from the list
    int destroy_block_entry(void *user_context, BlockEntry *block_entry);

    // Invokes the allocation callback to allocate memory for the block region
    int alloc_memory_block(void *user_context, BlockResource *block);

    // Invokes the deallocation callback to free memory for the memory block
    int free_memory_block(void *user_context, BlockResource *block);

    // Returns a constrained size for the requested size based on config parameters
    size_t constrain_requested_size(size_t size) const;

    // Returns true if the given block is compatible with the given properties
    bool is_compatible_block(const BlockResource *block, const MemoryProperties &properties) const;

    // Returns true if the given block is suitable for the request allocation
    bool is_block_suitable_for_request(void *user_context, const BlockResource *block, const MemoryProperties &properties, size_t size, bool dedicated) const;

    Config config;
    LinkedList block_list;
    MemoryAllocators allocators;
};

BlockAllocator *BlockAllocator::create(void *user_context, const Config &cfg, const MemoryAllocators &allocators) {
    halide_abort_if_false(user_context, allocators.system.allocate != nullptr);
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
    halide_abort_if_false(user_context, instance != nullptr);
    const MemoryAllocators &allocators = instance->allocators;
    instance->destroy(user_context);
    halide_abort_if_false(user_context, allocators.system.deallocate != nullptr);
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
#ifdef DEBUG_RUNTIME_INTERNAL
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
        error(user_context) << "BlockAllocator: Failed to allocate new empty block of requested size ("
                            << (int32_t)(request.size) << " bytes)!\n";
        return nullptr;
    }

    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    halide_abort_if_false(user_context, block != nullptr);
    halide_abort_if_false(user_context, block->allocator != nullptr);

    MemoryRegion *result = reserve_memory_region(user_context, block->allocator, request);
    if (result == nullptr) {

        // Unable to reserve region in an existing block ... create a new block and try again.
        block_entry = create_block_entry(user_context, request.properties, request.size, request.dedicated);
        if (block_entry == nullptr) {
            error(user_context) << "BlockAllocator: Out of memory! Failed to allocate empty block of size ("
                                << (int32_t)(request.size) << " bytes)!\n";
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

int BlockAllocator::release(void *user_context, MemoryRegion *memory_region) {
    if (memory_region == nullptr) {
        return halide_error_code_internal_error;
    }
    RegionAllocator *allocator = RegionAllocator::find_allocator(user_context, memory_region);
    if (allocator == nullptr) {
        return halide_error_code_internal_error;
    }
    return allocator->release(user_context, memory_region);
}

int BlockAllocator::reclaim(void *user_context, MemoryRegion *memory_region) {
    if (memory_region == nullptr) {
        return halide_error_code_internal_error;
    }
    RegionAllocator *allocator = RegionAllocator::find_allocator(user_context, memory_region);
    if (allocator == nullptr) {
        return halide_error_code_internal_error;
    }
    return allocator->reclaim(user_context, memory_region);
}

int BlockAllocator::retain(void *user_context, MemoryRegion *memory_region) {
    if (memory_region == nullptr) {
        return halide_error_code_internal_error;
    }
    RegionAllocator *allocator = RegionAllocator::find_allocator(user_context, memory_region);
    if (allocator == nullptr) {
        return halide_error_code_internal_error;
    }
    return allocator->retain(user_context, memory_region);
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

#ifdef DEBUG_RUNTIME_INTERNAL
        uint64_t reserved = block->reserved;
#endif

        bool collected = block->allocator->collect(user_context);
        if (collected) {
#ifdef DEBUG_RUNTIME_INTERNAL
            debug(user_context) << "Collected block ("
                                << "block=" << (void *)block << " "
                                << "reserved=" << (uint32_t)block->reserved << " "
                                << "recovered=" << (uint32_t)(reserved - block->reserved) << " "
                                << ")\n";
#endif
        }
        if (block->reserved == 0) {
            destroy_block_entry(user_context, block_entry);
            result = true;
        }

        block_entry = prev_entry;
    }
    return result;
}

int BlockAllocator::release(void *user_context) {
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;
        release_block_entry(user_context, block_entry);
        block_entry = prev_entry;
    }
    return 0;
}

int BlockAllocator::destroy(void *user_context) {
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;
        destroy_block_entry(user_context, block_entry);
        block_entry = prev_entry;
    }
    block_list.destroy(user_context);
    return 0;
}

MemoryRegion *BlockAllocator::reserve_memory_region(void *user_context, RegionAllocator *allocator, const MemoryRequest &request) {
    MemoryRegion *result = allocator->reserve(user_context, request);
    if (result == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
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

bool BlockAllocator::is_block_suitable_for_request(void *user_context, const BlockResource *block, const MemoryProperties &properties, size_t size, bool dedicated) const {
    if (!is_compatible_block(block, properties)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "BlockAllocator: skipping block ... incompatible properties!\n"
                            << " block_resource=" << (void *)block << "\n"
                            << " block_size=" << (uint32_t)block->memory.size << "\n"
                            << " block_reserved=" << (uint32_t)block->reserved << "\n"
                            << " block_usage=" << halide_memory_usage_name(block->memory.properties.usage) << "\n"
                            << " block_caching=" << halide_memory_caching_name(block->memory.properties.caching) << "\n"
                            << " block_visibility=" << halide_memory_visibility_name(block->memory.properties.visibility) << "\n";
        debug(user_context) << " request_size=" << (uint32_t)size << "\n"
                            << " request_usage=" << halide_memory_usage_name(properties.usage) << "\n"
                            << " request_caching=" << halide_memory_caching_name(properties.caching) << "\n"
                            << " request_visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
#endif
        // skip blocks that are using incompatible memory
        return false;
    }

    if (dedicated && (block->reserved > 0)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "BlockAllocator: skipping block ... can be used for dedicated allocation!\n"
                            << " block_resource=" << (void *)block << "\n"
                            << " block_size=" << (uint32_t)block->memory.size << "\n"
                            << " block_reserved=" << (uint32_t)block->reserved << "\n";
#endif
        // skip blocks that can't be dedicated to a single allocation
        return false;

    } else if (block->memory.dedicated && (block->reserved > 0)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "BlockAllocator: skipping block ... already dedicated to an allocation!\n"
                            << " block_resource=" << (void *)block << "\n"
                            << " block_size=" << (uint32_t)block->memory.size << "\n"
                            << " block_reserved=" << (uint32_t)block->reserved << "\n";
#endif
        // skip dedicated blocks that are already allocated
        return false;
    }

    size_t available = (block->memory.size - block->reserved);
    if (available >= size) {
        return true;
    }

    return false;
}

BlockAllocator::BlockEntry *
BlockAllocator::find_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
    BlockEntry *block_entry = block_list.back();
    while (block_entry != nullptr) {
        BlockEntry *prev_entry = block_entry->prev_ptr;
        const BlockResource *block = static_cast<BlockResource *>(block_entry->value);
        if (is_block_suitable_for_request(user_context, block, properties, size, dedicated)) {
#ifdef DEBUG_RUNTIME_INTERNAL
            debug(user_context) << "BlockAllocator: found suitable block ...\n"
                                << " user_context=" << (void *)(user_context) << "\n"
                                << " block_resource=" << (void *)block << "\n"
                                << " block_size=" << (uint32_t)block->memory.size << "\n"
                                << " block_reserved=" << (uint32_t)block->reserved << "\n"
                                << " request_size=" << (uint32_t)size << "\n"
                                << " dedicated=" << (dedicated ? "true" : "false") << "\n"
                                << " usage=" << halide_memory_usage_name(properties.usage) << "\n"
                                << " caching=" << halide_memory_caching_name(properties.caching) << "\n"
                                << " visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
#endif
            return block_entry;
        }
        block_entry = prev_entry;
    }

    if (block_entry == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "BlockAllocator: couldn't find suitable block!\n"
                            << " user_context=" << (void *)(user_context) << "\n"
                            << " request_size=" << (uint32_t)size << "\n"
                            << " dedicated=" << (dedicated ? "true" : "false") << "\n"
                            << " usage=" << halide_memory_usage_name(properties.usage) << "\n"
                            << " caching=" << halide_memory_caching_name(properties.caching) << "\n"
                            << " visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
#endif
    }
    return block_entry;
}

BlockAllocator::BlockEntry *
BlockAllocator::reserve_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: reserving block ... !\n"
                        << " requested_size=" << (uint32_t)size << "\n"
                        << " requested_is_dedicated=" << (dedicated ? "true" : "false") << "\n"
                        << " requested_usage=" << halide_memory_usage_name(properties.usage) << "\n"
                        << " requested_caching=" << halide_memory_caching_name(properties.caching) << "\n"
                        << " requested_visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
#endif
    BlockEntry *block_entry = find_block_entry(user_context, properties, size, dedicated);
    if (block_entry == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "BlockAllocator: creating block ... !\n"
                            << " requested_size=" << (uint32_t)size << "\n"
                            << " requested_is_dedicated=" << (dedicated ? "true" : "false") << "\n"
                            << " requested_usage=" << halide_memory_usage_name(properties.usage) << "\n"
                            << " requested_caching=" << halide_memory_caching_name(properties.caching) << "\n"
                            << " requested_visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
#endif
        block_entry = create_block_entry(user_context, properties, size, dedicated);
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
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Creating region allocator ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_resource=" << (void *)(block) << ")...\n";
#endif
    halide_abort_if_false(user_context, block != nullptr);
    RegionAllocator *region_allocator = RegionAllocator::create(
        user_context, block, {allocators.system, allocators.region});

    if (region_allocator == nullptr) {
        error(user_context) << "BlockAllocator: Failed to create new region allocator!\n";
        return nullptr;
    }

    return region_allocator;
}

int BlockAllocator::destroy_region_allocator(void *user_context, RegionAllocator *region_allocator) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Destroying region allocator ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "region_allocator=" << (void *)(region_allocator) << ")...\n";
#endif
    if (region_allocator == nullptr) {
        return 0;
    }
    return RegionAllocator::destroy(user_context, region_allocator);
}

BlockAllocator::BlockEntry *
BlockAllocator::create_block_entry(void *user_context, const MemoryProperties &properties, size_t size, bool dedicated) {
    if (config.maximum_pool_size && (pool_size() >= config.maximum_pool_size)) {
        error(user_context) << "BlockAllocator: No free blocks found! Maximum pool size reached ("
                            << (int32_t)(config.maximum_pool_size) << " bytes or "
                            << (int32_t)(config.maximum_pool_size / (1024 * 1024)) << " MB)\n";
        return nullptr;
    }

    if (config.maximum_block_count && (block_count() >= config.maximum_block_count)) {
        error(user_context) << "BlockAllocator: No free blocks found! Maximum block count reached ("
                            << (int32_t)(config.maximum_block_count) << ")!\n";
        return nullptr;
    }

    BlockEntry *block_entry = block_list.append(user_context);
    if (block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new block entry!\n";
        return nullptr;
    }

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Creating block entry ("
                        << "block_entry=" << (void *)(block_entry) << " "
                        << "block=" << (void *)(block_entry->value) << " "
                        << "allocator=" << (void *)(allocators.block.allocate) << ")...\n";
#endif

    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    block->memory.size = constrain_requested_size(size);
    block->memory.handle = nullptr;
    block->memory.properties = properties;
    block->memory.properties.nearest_multiple = max(config.nearest_multiple, properties.nearest_multiple);
    block->memory.dedicated = dedicated;
    block->reserved = 0;
    block->allocator = create_region_allocator(user_context, block);
    alloc_memory_block(user_context, block);
    return block_entry;
}

int BlockAllocator::release_block_entry(void *user_context, BlockAllocator::BlockEntry *block_entry) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Releasing block entry ("
                        << "block_entry=" << (void *)(block_entry) << " "
                        << "block=" << (void *)(block_entry->value) << ")...\n";
#endif
    BlockResource *block = static_cast<BlockResource *>(block_entry->value);
    if (block->allocator) {
        return block->allocator->release(user_context);
    }
    return 0;
}

int BlockAllocator::destroy_block_entry(void *user_context, BlockAllocator::BlockEntry *block_entry) {
#ifdef DEBUG_RUNTIME_INTERNAL
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
    return 0;
}

int BlockAllocator::alloc_memory_block(void *user_context, BlockResource *block) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Allocating block (ptr=" << (void *)block << " allocator=" << (void *)allocators.block.allocate << ")...\n";
#endif
    halide_abort_if_false(user_context, allocators.block.allocate != nullptr);
    MemoryBlock *memory_block = &(block->memory);
    allocators.block.allocate(user_context, memory_block);
    block->reserved = 0;
    return 0;
}

int BlockAllocator::free_memory_block(void *user_context, BlockResource *block) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "BlockAllocator: Deallocating block (ptr=" << (void *)block << " allocator=" << (void *)allocators.block.deallocate << ")...\n";
#endif
    halide_abort_if_false(user_context, allocators.block.deallocate != nullptr);
    MemoryBlock *memory_block = &(block->memory);
    allocators.block.deallocate(user_context, memory_block);
    memory_block->handle = nullptr;
    block->reserved = 0;
    block->memory.size = 0;
    return 0;
}

size_t BlockAllocator::constrain_requested_size(size_t size) const {
    size_t actual_size = size;
    if (config.nearest_multiple) {
        actual_size = (((actual_size + config.nearest_multiple - 1) / config.nearest_multiple) * config.nearest_multiple);
    }
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

size_t BlockAllocator::pool_size() const {
    size_t total_size = 0;
    BlockEntry const *block_entry = nullptr;
    for (block_entry = block_list.front(); block_entry != nullptr; block_entry = block_entry->next_ptr) {
        const BlockResource *block = static_cast<BlockResource *>(block_entry->value);
        if (block != nullptr) {
            total_size += block->memory.size;
        }
    }
    return total_size;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

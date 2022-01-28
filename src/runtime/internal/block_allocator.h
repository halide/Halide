#ifndef HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
#define HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

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

    // disable copy constructors and assignment
    BlockAllocator(const BlockAllocator &) = delete;
    BlockAllocator &operator=(const BlockAllocator &) = delete;

public:

    typedef LinkedList<BlockResource> BlockResourceList;
    typedef BlockResourceList::EntryType BlockEntry;

    typedef LinkedList<RegionAllocator> AllocatorList;
    typedef AllocatorList::EntryType AllocatorEntry;

    struct MemoryAllocators {
        SystemMemoryAllocator* system = nullptr;
        MemoryBlockAllocator* block = nullptr;
        MemoryRegionAllocator* region = nullptr;
    };

    struct Config {
        size_t minimum_block_size = 0;
        size_t maximum_block_size = 0;
        size_t maximum_block_count = 0;
    };

public:

    // Factory methods for creation / destruction
    static BlockAllocator* create(void* user_context, const Config& config, const MemoryAllocators& allocators);
    static void destroy(void* user_context, BlockAllocator* block_allocator);

    // Public interface methods
    MemoryRegion* reserve(void *user_context, MemoryBusAccess access, size_t size, size_t alignment);
    void reclaim(void *user_context, MemoryRegion* region);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);    

    // Access methods 
    const Config& current_config() const;
    const Config& default_config() const;
    size_t block_count() const;

private:

    BlockAllocator() = default;
    ~BlockAllocator() = default;

    void initialize(void* user_context, const Config& config, const MemoryAllocators& allocators);

    MemoryRegion* reserve_memory_region(void* user_context, RegionAllocator* allocator, size_t size, size_t alignment);

    AllocatorEntry* find_region_allocator(void* user_context, BlockResource* block);
    AllocatorEntry* create_region_allocator(void* user_context, BlockResource* block);
    void destroy_region_allocator(void* user_context, AllocatorEntry* alloc_entry);

    BlockEntry* reserve_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    BlockEntry* find_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    BlockEntry* create_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    void destroy_block_entry(void* user_context, BlockEntry* block_entry);

    void alloc_memory_block(void* user_context, BlockResource* block);
    void free_memory_block(void* user_context, BlockResource* block);

    size_t constrain_requested_size(size_t size) const;

private:

    Config config;
    BlockResourceList block_list;
    AllocatorList allocator_list;
    MemoryAllocators allocators;
};


BlockAllocator* BlockAllocator::create(void* user_context, const Config& cfg, const MemoryAllocators& allocators) {
    halide_abort_if_false(user_context, allocators.system != nullptr);
    BlockAllocator* result = reinterpret_cast<BlockAllocator*>(
        allocators.system->allocate(user_context, sizeof(BlockAllocator))
    );

    if(result == nullptr) {
        error(user_context) << "BlockAllocator: Failed to create instance! Out of memory!\n";
        return nullptr; 
    }

    result->initialize(user_context, cfg, allocators);
    return result;
}

void BlockAllocator::destroy(void* user_context, BlockAllocator* instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    const MemoryAllocators& allocators = instance->allocators;
    instance->destroy(user_context);
    halide_abort_if_false(user_context, allocators.system != nullptr);
    allocators.system->deallocate(user_context, instance);
}

void BlockAllocator::initialize(void* user_context, const Config& cfg, const MemoryAllocators& ma) {
    config = cfg;
    allocators = ma;
    block_list.initialize(user_context, BlockResourceList::default_capacity, allocators.system);
    allocator_list.initialize(user_context, AllocatorList::default_capacity, allocators.system); 
}

MemoryRegion* BlockAllocator::reserve(void *user_context, MemoryBusAccess access, size_t size, size_t alignment) {

    BlockEntry* block_entry = reserve_block_entry(user_context, access, size);
    if(block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new empty block of requested size (" 
                            << (int32_t)(size) << " bytes)!\n";
        return nullptr; 
    }

    BlockResource* block = &(block_entry->value);
    halide_abort_if_false(user_context, block != nullptr);
    halide_abort_if_false(user_context, block->allocator != nullptr);

    MemoryRegion* result = reserve_memory_region(user_context, block->allocator, size, alignment);
    if(result == nullptr) {
        
        size_t actual_size = constrain_requested_size(size);
        debug(user_context) << "BlockAllocator: No free blocks found! Allocating new empty block of size (" 
                            << (int32_t)(actual_size) << " bytes)!\n";

        // Unable to reserve region in an existing block ... create a new block and try again.
        block_entry = create_block_entry(user_context, access, actual_size);
        if(block_entry == nullptr) {
            debug(user_context) << "BlockAllocator: Out of memory! Failed to allocate empty block of size (" 
                                << (int32_t)(actual_size) << " bytes)!\n";
            return nullptr;
        }

        block = &(block_entry->value);
        if(block->allocator == nullptr) {
            create_region_allocator(user_context, block);
        }

        result = reserve_memory_region(user_context, block->allocator, size, alignment);
    }
    return result;
}

void BlockAllocator::reclaim(void *user_context, MemoryRegion* memory_region) {
    halide_abort_if_false(user_context, memory_region != nullptr);
    RegionAllocator* allocator = RegionAllocator::find_allocator(user_context, memory_region);
    if(allocator == nullptr) { return; }
    allocator->reclaim(user_context, memory_region);
}

bool BlockAllocator::collect(void* user_context) {
    bool result = false;
    BlockEntry* block_entry = block_list.front();
    while(block_entry != nullptr) {
        
        const BlockResource* block = &(block_entry->value);
        if(block->allocator == nullptr) {
            continue;
        }

        block->allocator->collect(user_context);
        if(block->reserved == 0) {
            destroy_block_entry(user_context, block_entry);
            result = true;
        }

        block_entry = block_entry->next_ptr;
    }    
    return result;
}

void BlockAllocator::destroy(void *user_context) {
    BlockEntry* block_entry = block_list.front();
    while(block_entry != nullptr) {
        destroy_block_entry(user_context, block_entry);
        block_entry = block_entry->next_ptr;
    }    
}

MemoryRegion* BlockAllocator::reserve_memory_region(void* user_context, RegionAllocator* allocator, size_t size, size_t alignment) {
    MemoryRegion* result = allocator->reserve(user_context, size, alignment);
    if(result == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate region of size (" 
                            << (int32_t)(size) << " bytes)!\n";

        // allocator has enough free space, but not enough contiguous space
        // -- collect and try to reallocate
        if(allocator->collect(user_context)) {
            result = allocator->reserve(user_context, size, alignment);
        }
    }
    return result;
}

BlockAllocator::BlockEntry* 
BlockAllocator::find_block_entry(void* user_context, MemoryBusAccess access, size_t size) {
    BlockEntry* block_entry = block_list.front();
    while(block_entry != nullptr) {
        
        const BlockResource* block = &(block_entry->value);
        if(block->access != access) {
            continue;
        }

        size_t available = (block->size - block->reserved);
        if(available >= size) {
            break;
        }

        block_entry = block_entry->next_ptr;
    }
    return block_entry;
}

BlockAllocator::BlockEntry* 
BlockAllocator::reserve_block_entry(void *user_context, MemoryBusAccess access, size_t size) {
    BlockEntry* block_entry = find_block_entry(user_context, access, size);
    if(block_entry == nullptr) {
        size_t actual_size = constrain_requested_size(size);
        debug(user_context) << "BlockAllocator: No free blocks found! Allocating new empty block of size (" 
                            << (int32_t)(actual_size) << " bytes)!\n";

        block_entry = create_block_entry(user_context, access, actual_size);
    }

    if(block_entry) {
        BlockResource* block = &(block_entry->value);
        if(block->allocator == nullptr) {
            create_region_allocator(user_context, block);
        }
    }
    return block_entry;
}
 
BlockAllocator::AllocatorEntry*
BlockAllocator::find_region_allocator(void* user_context, BlockResource* block) {
    AllocatorEntry* region_allocator = allocator_list.front();
    while(region_allocator != nullptr) {
        if(&(region_allocator->value) == block->allocator ) {
            break;
        }
        region_allocator = region_allocator->next_ptr;
    }
    return region_allocator;
}

BlockAllocator::AllocatorEntry*
BlockAllocator::create_region_allocator(void* user_context, BlockResource* block) {
    AllocatorEntry* region_allocator = allocator_list.append(user_context);
    RegionAllocator* allocator = &(region_allocator->value);
    memset(allocator, 0, sizeof(RegionAllocator));
    allocator->initialize(user_context, block, {allocators.system, allocators.region});
    block->allocator = allocator;
    return region_allocator;
}

void BlockAllocator::destroy_region_allocator(void* user_context, BlockAllocator::AllocatorEntry* region_allocator) {
    allocator_list.remove(user_context, region_allocator);
}

BlockAllocator::BlockEntry* 
BlockAllocator::create_block_entry(void* user_context, MemoryBusAccess access, size_t size) {
    
    if(config.maximum_block_count && (block_count() >= config.maximum_block_count)) {
        error(user_context) << "BlockAllocator: No free blocks found! Maximum block count reached (" 
                            << (int32_t)(config.maximum_block_count) << ")!\n";
        return nullptr;
    }

    BlockEntry* block_entry = block_list.append(user_context);
    BlockResource* block = &(block_entry->value);
    memset(block, 0, sizeof(BlockResource));
    block->size = size;
    block->reserved = 0;
    block->allocator = nullptr;
    block->access = access;
    alloc_memory_block(user_context, block);
    return block_entry;
}

void BlockAllocator::destroy_block_entry(void* user_context, BlockAllocator::BlockEntry* block_entry) {
    BlockResource* block = &(block_entry->value);
    free_memory_block(user_context, block);
    block_list.remove(user_context, block_entry);
}

void BlockAllocator::alloc_memory_block(void* user_context, BlockResource* block) {
    // Cast into client-facing memory block struct for allocation request
    MemoryBlock* memory_block = reinterpret_cast<MemoryBlock*>(block);
    allocators.block->allocate(user_context, memory_block);
    block->reserved = 0;
}

void BlockAllocator::free_memory_block(void* user_context, BlockResource* block) {
    if(block->allocator) {
        block->allocator->destroy(user_context);
        AllocatorEntry* region_allocator = find_region_allocator(user_context, block);
        if(region_allocator != nullptr) {
            destroy_region_allocator(user_context, region_allocator);
            block->allocator = nullptr;
        }
    }

    // Cast into client-facing memory block struct for deallocation request
    MemoryBlock* memory_block = reinterpret_cast<MemoryBlock*>(block);
    allocators.block->deallocate(user_context, memory_block);
    block->reserved = 0;
    block->size = 0;
}

size_t BlockAllocator::constrain_requested_size(size_t size) const {
    size_t actual_size = size;
    if(config.minimum_block_size) {
        actual_size = (actual_size > config.minimum_block_size) ? actual_size : config.minimum_block_size;
    }
    if(config.maximum_block_size) {
        actual_size = (actual_size < config.maximum_block_size) ? actual_size : config.maximum_block_size;
    }
    return actual_size;
}

const BlockAllocator::Config& BlockAllocator::current_config() const {
    return config;
}

const BlockAllocator::Config& BlockAllocator::default_config() const {
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
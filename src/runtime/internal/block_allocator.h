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

    typedef void* (*AllocMemoryFn)(void*, size_t bytes);
    typedef void (*FreeMemoryFn)(void*, void* ptr);
    typedef void (*AllocBlockFn)(void*, MemoryBlock*);
    typedef void (*FreeBlockFn)(void*, MemoryBlock*);
    typedef void (*AllocRegionFn)(void*, MemoryRegion*);
    typedef void (*FreeRegionFn)(void*, MemoryRegion*);

    struct AllocBlockRegionFns {
        AllocMemoryFn alloc_memory;
        FreeMemoryFn free_memory;
        AllocBlockFn alloc_block;
        FreeBlockFn free_block;
        AllocRegionFn alloc_region;
        FreeRegionFn free_region;
    };

public:

    // Factory methods for creation / destruction
    static BlockAllocator* create(void* user_context, size_t block_size, const AllocBlockRegionFns& allocator_fns);
    static void destroy(void* user_context, BlockAllocator* block_allocator);

    // Public interface methods
    MemoryRegion* reserve(void *user_context, MemoryBusAccess access, size_t size, size_t alignment);
    void reclaim(void *user_context, MemoryRegion* region);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);    

private:

    BlockAllocator() = default;
    ~BlockAllocator() = default;

    void initialize(void* user_context, size_t block_size, const AllocBlockRegionFns& allocator_fns);

    MemoryRegion* reserve_memory_region(void* user_context, RegionAllocator* allocator, size_t size, size_t alignment);

    AllocatorEntry* find_allocator_entry(void* user_context, BlockResource* block);
    AllocatorEntry* create_allocator_entry(void* user_context, BlockResource* block);
    void destroy_allocator_entry(void* user_context, AllocatorEntry* alloc_entry);

    BlockEntry* reserve_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    BlockEntry* find_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    BlockEntry* create_block_entry(void* user_context, MemoryBusAccess access, size_t size);
    void destroy_block_entry(void* user_context, BlockEntry* block_entry);

    void alloc_memory_block(void* user_context, BlockResource* block);
    void free_memory_block(void* user_context, BlockResource* block);

private:

    size_t block_size;
    BlockResourceList block_list;
    AllocatorList allocator_list;
    AllocBlockRegionFns alloc;
};


BlockAllocator* BlockAllocator::create(void* user_context, size_t block_size, const AllocBlockRegionFns& alloc_fns) {
    BlockAllocator* result = reinterpret_cast<BlockAllocator*>(
        alloc_fns.alloc_memory(user_context, sizeof(BlockAllocator))
    );

    if(result == nullptr) {
        error(user_context) << "BlockAllocator: Failed to create instance! Out of memory!\n";
        return nullptr; 
    }

    result->initialize(user_context, block_size, alloc_fns);
    return result;
}

void BlockAllocator::destroy(void* user_context, BlockAllocator* instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    const AllocBlockRegionFns& alloc = instance->alloc;
    instance->destroy(user_context);
    alloc.free_memory(user_context, instance);
}

void BlockAllocator::initialize(void* user_context, size_t bs, const AllocBlockRegionFns& alloc_fns) {
    block_size = bs;
    alloc = alloc_fns;
    block_list.initialize(user_context, BlockResourceList::default_capacity, {alloc.alloc_memory, alloc.free_memory});
    allocator_list.initialize(user_context, AllocatorList::default_capacity, {alloc.alloc_memory, alloc.free_memory}); 
}

MemoryRegion* BlockAllocator::reserve(void *user_context, MemoryBusAccess access, size_t size, size_t alignment) {

    BlockEntry* block_entry = reserve_block_entry(user_context, access, size);
    if(block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new empty block of size (" 
                            << (int32_t)(block_size) << " bytes)!\n";
        return nullptr; 
    }

    BlockResource* block = &(block_entry->value);
    halide_abort_if_false(user_context, block != nullptr);
    halide_abort_if_false(user_context, block->allocator != nullptr);

    MemoryRegion* result = reserve_memory_region(user_context, block->allocator, size, alignment);
    if(result == nullptr) {
        
        size_t actual_size = (size > block_size) ? size : block_size;
        debug(user_context) << "BlockAllocator: No free blocks found! Allocating new empty block of size (" 
                            << (int32_t)(actual_size) << " bytes)!\n";

        // Unable to reserve region in an existing block ... create a new block and try again.
        block_entry = create_block_entry(user_context, access, actual_size);
        if(block_entry == nullptr) {
            debug(user_context) << "BlockAllocator: Out of memory! Failed to allocate empty block of size (" 
                                << (int32_t)(actual_size) << " bytes)!\n";
        }

        block = &(block_entry->value);
        if(block->allocator == nullptr) {
            create_allocator_entry(user_context, block);
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
        size_t actual_size = (size > block_size) ? size : block_size;
        debug(user_context) << "BlockAllocator: No free blocks found! Allocating new empty block of size (" 
                            << (int32_t)(actual_size) << " bytes)!\n";

        block_entry = create_block_entry(user_context, access, actual_size);
    }

    if(block_entry) {
        BlockResource* block = &(block_entry->value);
        if(block->allocator == nullptr) {
            create_allocator_entry(user_context, block);
        }
    }
    return block_entry;
}
 
BlockAllocator::AllocatorEntry*
BlockAllocator::find_allocator_entry(void* user_context, BlockResource* block) {
    AllocatorEntry* allocator_entry = allocator_list.front();
    while(allocator_entry != nullptr) {
        if(&(allocator_entry->value) == block->allocator ) {
            break;
        }
        allocator_entry = allocator_entry->next_ptr;
    }
    return allocator_entry;
}

BlockAllocator::AllocatorEntry*
BlockAllocator::create_allocator_entry(void* user_context, BlockResource* block) {
    AllocatorEntry* allocator_entry = allocator_list.append(user_context);
    RegionAllocator* allocator = &(allocator_entry->value);
    memset(allocator, 0, sizeof(RegionAllocator));
    allocator->initialize(user_context, block, 
        {alloc.alloc_memory, alloc.free_memory, alloc.alloc_region, alloc.free_region}
    );
    block->allocator = allocator;
    return allocator_entry;
}

void BlockAllocator::destroy_allocator_entry(void* user_context, BlockAllocator::AllocatorEntry* allocator_entry) {
    allocator_list.remove(user_context, allocator_entry);
}

BlockAllocator::BlockEntry* 
BlockAllocator::create_block_entry(void* user_context, MemoryBusAccess access, size_t size) {
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
    alloc.alloc_block(user_context, memory_block);
    block->reserved = 0;
}

void BlockAllocator::free_memory_block(void* user_context, BlockResource* block) {
    if(block->allocator) {
        block->allocator->destroy(user_context);
        AllocatorEntry* allocator_entry = find_allocator_entry(user_context, block);
        if(allocator_entry != nullptr) {
            destroy_allocator_entry(user_context, allocator_entry);
            block->allocator = nullptr;
        }
    }

    // Cast into client-facing memory block struct for deallocation request
    MemoryBlock* memory_block = reinterpret_cast<MemoryBlock*>(block);
    alloc.free_block(user_context, memory_block);
    block->reserved = 0;
    block->size = 0;
}

// --
    
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
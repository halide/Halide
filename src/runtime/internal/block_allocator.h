#ifndef HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
#define HALIDE_RUNTIME_BLOCK_ALLOCATOR_H

#include "linked_list.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --

enum MemoryUsage {
    InvalidUsage,
    InUse,
    Available,
    Dedicated
};

enum MemoryAccess {
    InvalidAccess,
    HostOnly,
    DeviceOnly,
    DeviceToHost,
    HostToDevice,
};

class RegionAllocator;
struct MemoryRegion;
struct MemoryBlock {
    void* handle;                   //< client data storing native handle (managed by alloc_block/free_block)
    size_t size;                    //< allocated size in bytes
    size_t reserved;                //< number of bytes already reserved to regions
    MemoryAccess access;            //< visibility access for the allocated block 
    MemoryRegion* regions;          //< head of linked list of memory regions
    RegionAllocator* allocator;     //< designated allocator for the block
};

struct MemoryRegion {
    void* handle;                   //< client data storing native handle (managed by alloc_region/free_region)
    size_t offset;                  //< offset from parent block base ptr (in bytes)
    size_t size;                    //< allocated size in bytes
    MemoryUsage usage;              //< usage indicator
    MemoryRegion* next_ptr;         //< pointer to next memory region in linked list
    MemoryRegion* prev_ptr;         //< pointer to prev memory region in linked list
    MemoryBlock* block_ptr;         //< pointer to parent memory block
};

class RegionAllocator {

    // disable copy constructors and assignment
    RegionAllocator(const RegionAllocator &) = delete;
    RegionAllocator &operator=(const RegionAllocator &) = delete;

public:
    static const uint32_t InvalidEntry = uint32_t(-1);
    typedef MemoryArena<MemoryRegion> RegionArena;

    typedef void (*AllocRegionFn)(void*, MemoryRegion*);
    typedef void (*FreeRegionFn)(void*, MemoryRegion*);
    typedef void* (*AllocMemoryFn)(void*, size_t bytes);
    typedef void (*FreeMemoryFn)(void*, void* ptr);

    struct AllocRegionFns {
        AllocMemoryFn alloc_memory;
        FreeMemoryFn free_memory;
        AllocRegionFn alloc_region;
        FreeRegionFn free_region;
    };

    RegionAllocator(void* user_context, MemoryBlock* block, const AllocRegionFns& allocator_fns);
    ~RegionAllocator();

    void initialize(void* user_context, MemoryBlock* block, const AllocRegionFns& allocator_fns);
    MemoryRegion* reserve(void *user_context, size_t size, size_t alignment);
    void reclaim(void *user_context, MemoryRegion* region);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);

private:

    MemoryRegion* find_region(void* user_context, size_t size, size_t alignment);
    bool can_coalesce(MemoryRegion* region);
    MemoryRegion* coalesce_region(void* user_context, MemoryRegion* region);
    bool can_split(MemoryRegion* region, size_t size);
    MemoryRegion* split_region(void* user_context, MemoryRegion* region, size_t size, size_t alignment);
    MemoryRegion* create_region(void* user_context, size_t offset, size_t size);
    void destroy_region(void* user_context, MemoryRegion* region);

    void alloc_region(void* user_context, MemoryRegion* region);
    void free_region(void* user_context, MemoryRegion* region);

    inline size_t aligned_offset(size_t offset, size_t alignment) {
        return (offset + (alignment - 1)) & ~(alignment - 1);
    }

    inline size_t aligned_size(size_t offset, size_t size, size_t alignment) {
        size_t actual_offset = aligned_offset(offset, alignment);
        size_t padding = actual_offset - offset;
        size_t actual_size = padding + size;
        return actual_size;
    }

private:
    MemoryBlock* block;
    RegionArena arena;
    AllocRegionFns alloc;
};

RegionAllocator::RegionAllocator(void* user_context, MemoryBlock* mb, const AllocRegionFns& fns) :
    block(mb), 
    arena(nullptr, RegionArena::default_capacity, 
          {nullptr, nullptr}, 
          {fns.alloc_memory, fns.free_memory}), 
    alloc(fns) {
    
    block->allocator = this;
    block->regions = create_region(user_context, 0, block->size);
}

RegionAllocator::~RegionAllocator() {
    destroy(nullptr);
}

void RegionAllocator::initialize(void* user_context, MemoryBlock* mb, const AllocRegionFns& fns) {
    block = mb;
    alloc = fns;
    arena.initialize(
        user_context, 
        RegionArena::default_capacity, 
        {nullptr, nullptr}, 
        {fns.alloc_memory, fns.free_memory}
    );
    block->allocator = this;
    block->regions = create_region(user_context, 0, block->size);
}

MemoryRegion* RegionAllocator::reserve(void *user_context, size_t size, size_t alignment) {
    size_t remaining = block->size - block->reserved;
    if(remaining < size) { 
        debug(user_context) << "RegionAllocator: Unable to reserve more memory from block " 
                            << "-- requested size (" << (int32_t)(size) << " bytes) "
                            << "greater than available (" << (int32_t)(remaining) << " bytes)!\n";    
        return nullptr; 
    }

    MemoryRegion* region = find_region(user_context, size, alignment);
    if(region == nullptr) {
        debug(user_context) << "RegionAllocator: Falied to locate region for requested size (" 
                            << (int32_t)(size) << " bytes)!\n";

        return nullptr;
    }
    
    if(can_split(region, size)) {
        debug(user_context) << "RegionAllocator: Splitting region of size ( " << (int32_t)(region->size) << ") "
                            << "to accomodate requested size (" << (int32_t)(size) << " bytes)!\n";

        split_region(user_context, region, size, alignment);
    }

    alloc_region(user_context, region);    
    return region;
}

void RegionAllocator::reclaim(void* user_context, MemoryRegion* region) {
    halide_abort_if_false(user_context, region->block_ptr == block);
    free_region(user_context, region);
    if(can_coalesce(region)) {
        region = coalesce_region(user_context, region);
    }
}

MemoryRegion* RegionAllocator::find_region(void* user_context, size_t size, size_t alignment) {
    MemoryRegion* result = nullptr;
    for(MemoryRegion* region = block->regions; region != nullptr; region = region->next_ptr) {
        
        if(region->usage != MemoryUsage::Available) {
            continue;
        }

        if(size > region->size) {
            continue;
        }

        size_t actual_size = aligned_size(region->offset, size, alignment);
        if(actual_size > region->size) {
            continue;
        }

        if((actual_size + block->reserved) < block->size) {
            result = region;
            break;
        }
    }
    return result;
}

bool RegionAllocator::can_coalesce(MemoryRegion* region) {
    if(region == nullptr) { return false; }
    if(region->prev_ptr && (region->prev_ptr->usage == MemoryUsage::Available)) {
        return true;
    }
    if(region->next_ptr && (region->next_ptr->usage == MemoryUsage::Available)) {
        return true;
    }
    return false;
}

MemoryRegion* RegionAllocator::coalesce_region(void* user_context, MemoryRegion* region) {

    if(region->prev_ptr && (region->prev_ptr->usage == MemoryUsage::Available)) {
        MemoryRegion* prev_region = region->prev_ptr;

        debug(user_context) << "RegionAllocator: Coalescing "
                            << "previous region (offset=" << prev_region->offset << " size=" << (int32_t)(prev_region->size) << " bytes) " 
                            << "into current region (offset=" << region->offset << " size=" << (int32_t)(region->size) << " bytes)\n!";

        prev_region->next_ptr = region->next_ptr;
        if(region->next_ptr) {
            region->next_ptr->prev_ptr = prev_region;
        }
        prev_region->size += region->size;
        destroy_region(user_context, region);
        region = prev_region;
    }

    if(region->next_ptr && (region->next_ptr->usage == MemoryUsage::Available)) {
        MemoryRegion* next_region = region->next_ptr;

        debug(user_context) << "RegionAllocator: Coalescing "
                            << "next region (offset=" << next_region->offset << " size=" << (int32_t)(next_region->size) << " bytes) " 
                            << "into current region (offset=" << region->offset << " size=" << (int32_t)(region->size) << " bytes)!\n";

        if(next_region->next_ptr) {
            next_region->next_ptr->prev_ptr = region;
        }
        region->next_ptr = next_region->next_ptr;
        region->size += next_region->size;
        destroy_region(user_context, next_region);
    }

    return region;
}

bool RegionAllocator::can_split(MemoryRegion* region, size_t size) {
    return (region && (region->size > size));
}

MemoryRegion* RegionAllocator::split_region(void* user_context, MemoryRegion* region, size_t size, size_t alignment){

    size_t adjusted_size = aligned_size(region->offset, size, alignment);
    size_t adjusted_offset = aligned_offset(region->offset, alignment);

    size_t empty_offset = adjusted_offset + size;
    size_t empty_size = region->size - adjusted_size;

    debug(user_context) << "RegionAllocator: Splitting "
                        << "current region (offset=" << region->offset << " size=" << (int32_t)(region->size) << " bytes) " 
                        << "to create empty region (offset=" << empty_offset << " size=" << (int32_t)(empty_size) << " bytes)!\n";


    MemoryRegion* next_region = region->next_ptr;
    MemoryRegion* empty_region = create_region(user_context, empty_offset, empty_size);
    empty_region->next_ptr = next_region;
    if(next_region) { 
        next_region->prev_ptr = empty_region;
    }
    region->next_ptr = empty_region;
    region->size = size;
    return empty_region;
}

MemoryRegion* RegionAllocator::create_region(void* user_context, size_t offset, size_t size) {
    MemoryRegion* region = arena.reserve(user_context);
    memset(region, 0, sizeof(MemoryRegion));
    region->offset = offset;
    region->size = size;
    region->usage = MemoryUsage::Available;
    region->block_ptr = block;
    return region;
}
void RegionAllocator::destroy_region(void* user_context, MemoryRegion* region) {
    free_region(user_context, region);
    arena.reclaim(user_context, region);
}
void RegionAllocator::alloc_region(void* user_context, MemoryRegion* region) {
    debug(user_context) << "RegionAllocator: Allocating region of size ( " << (int32_t)(region->size) << ") bytes)!\n";
    halide_abort_if_false(user_context, region->usage == MemoryUsage::Available);
    alloc.alloc_region(user_context, region);
    region->usage = MemoryUsage::InUse;
    block->reserved += region->size;
}

void RegionAllocator::free_region(void* user_context, MemoryRegion* region) {
    if(region->usage == MemoryUsage::InUse) {
        debug(user_context) << "RegionAllocator: Freeing region of size ( " << (int32_t)(region->size) << ") bytes)!\n";
        alloc.free_region(user_context, region);
        block->reserved -= region->size;
    }
    region->usage = MemoryUsage::Available;
}

bool RegionAllocator::collect(void* user_context) {
    bool result = false;
    for(MemoryRegion* region = block->regions; region != nullptr; region = region->next_ptr) {
        if(region->usage == MemoryUsage::Available) {   
            if(can_coalesce(region)) {
                region = coalesce_region(user_context, region);
                result = true;
            }
        }
    }
    return result;
}

void RegionAllocator::destroy(void* user_context) {
    for(MemoryRegion* region = block->regions; region != nullptr; ) {
        
        if(region->next_ptr == nullptr) {
            destroy_region(user_context, region);
            region = nullptr;
        }
        else {
            MemoryRegion* previous = region;
            region = region->next_ptr;
            destroy_region(user_context, previous);
        }
    }
    block->regions = nullptr;
    block->reserved = 0;
    arena.destroy(user_context);
}

// --

class BlockAllocator {

    // disable copy constructors and assignment
    BlockAllocator(const BlockAllocator &) = delete;
    BlockAllocator &operator=(const BlockAllocator &) = delete;

public:

    typedef LinkedList<MemoryBlock> BlockList;
    typedef LinkedList<RegionAllocator> AllocatorList;

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

    BlockAllocator(void* user_context, size_t block_size, const AllocBlockRegionFns& allocator_fns);
    ~BlockAllocator();

    void initialize(void* user_context, size_t block_size, const AllocBlockRegionFns& allocator_fns);
    MemoryRegion* reserve(void *user_context, MemoryAccess access, size_t size, size_t alignment);
    void reclaim(void *user_context, MemoryRegion* region);
    bool collect(void* user_context); //< returns true if any blocks were removed
    void destroy(void *user_context);    

private:

    AllocatorList::EntryType* find_allocator_entry(void* user_context, MemoryBlock* block);
    AllocatorList::EntryType* create_allocator_entry(void* user_context, MemoryBlock* block);
    void destroy_allocator_entry(void* user_context, AllocatorList::EntryType* alloc_entry);

    BlockList::EntryType* find_block_entry(void* user_context, MemoryAccess access, size_t size);
    BlockList::EntryType* create_block_entry(void* user_context, MemoryAccess access, size_t size);
    void destroy_block_entry(void* user_context, BlockList::EntryType* block_entry);

    void alloc_block(void* user_context, MemoryBlock* block);
    void free_block(void* user_context, MemoryBlock* block);

    size_t block_size;
    BlockList block_list;
    AllocatorList allocator_list;
    AllocBlockRegionFns alloc;
};


BlockAllocator::BlockAllocator(void* user_context, size_t bs, const AllocBlockRegionFns& fns) :
    block_size(bs),
    block_list(user_context, BlockList::default_capacity, {fns.alloc_memory, fns.free_memory}), 
    allocator_list(user_context, AllocatorList::default_capacity, {fns.alloc_memory, fns.free_memory}), 
    alloc(fns) {
    // EMPTY!
}

BlockAllocator::~BlockAllocator() {
    destroy(nullptr);
}

void BlockAllocator::initialize(void* user_context, size_t bs, const AllocBlockRegionFns& alloc_fns) {
    block_size = bs;
    alloc = alloc_fns;
    block_list.initialize(user_context, BlockList::default_capacity, {alloc.alloc_memory, alloc.free_memory});
    allocator_list.initialize(user_context, AllocatorList::default_capacity, {alloc.alloc_memory, alloc.free_memory}); 
}

BlockAllocator::BlockList::EntryType* 
BlockAllocator::find_block_entry(void* user_context, MemoryAccess access, size_t size) {
    BlockList::EntryType* block_entry = block_list.front();
    while(block_entry != nullptr) {
        
        const MemoryBlock* block = &(block_entry->value);
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

MemoryRegion* BlockAllocator::reserve(void *user_context, MemoryAccess access, size_t size, size_t alignment) {

    MemoryRegion* region = nullptr;
    BlockList::EntryType* block_entry = find_block_entry(user_context, access, size);
    if(block_entry == nullptr) {
        size_t actual_size = (size > block_size) ? size : block_size;
        debug(user_context) << "BlockAllocator: No free blocks found! Allocating new empty block of size (" 
                            << (int32_t)(actual_size) << " bytes)!\n";

        block_entry = create_block_entry(user_context, access, actual_size);
    }

    if(block_entry == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate new empty block of size (" 
                            << (int32_t)(block_size) << " bytes)!\n";
        return nullptr; 
    }

    MemoryBlock* block = &(block_entry->value);
    if(block->allocator == nullptr) {
        create_allocator_entry(user_context, block);
    }

    halide_abort_if_false(user_context, block->allocator != nullptr);
    region = block->allocator->reserve(user_context, size, alignment);
    if(region == nullptr) {
        debug(user_context) << "BlockAllocator: Failed to allocate region of size (" 
                            << (int32_t)(size) << " bytes)!\n";
        return nullptr; 
    }
    return region;
}

void BlockAllocator::reclaim(void *user_context, MemoryRegion* region) {
    halide_debug_assert(user_context, region != nullptr);
    halide_debug_assert(user_context, region->block_ptr != nullptr);
    if(region == nullptr) { return; }
    if(region->block_ptr == nullptr) { return; }
    MemoryBlock* block = region->block_ptr;
    if(block == nullptr) { return; }
    if(block->allocator == nullptr) { return; }
    block->allocator->reclaim(user_context, region);
}

bool BlockAllocator::collect(void* user_context) {
    bool result = false;
    BlockList::EntryType* block_entry = block_list.front();
    while(block_entry != nullptr) {
        
        const MemoryBlock* block = &(block_entry->value);
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
    BlockList::EntryType* block_entry = block_list.front();
    while(block_entry != nullptr) {
        destroy_block_entry(user_context, block_entry);
        block_entry = block_entry->next_ptr;
    }    
}

BlockAllocator::AllocatorList::EntryType*
BlockAllocator::find_allocator_entry(void* user_context, MemoryBlock* block) {
    AllocatorList::EntryType* allocator_entry = allocator_list.front();
    while(allocator_entry != nullptr) {
        if(&(allocator_entry->value) == block->allocator ) {
            break;
        }
        allocator_entry = allocator_entry->next_ptr;
    }
    return allocator_entry;
}

BlockAllocator::AllocatorList::EntryType*
BlockAllocator::create_allocator_entry(void* user_context, MemoryBlock* block) {
    AllocatorList::EntryType* allocator_entry = allocator_list.append(user_context);
    RegionAllocator* allocator = &(allocator_entry->value);
    memset(allocator, 0, sizeof(RegionAllocator));
    allocator->initialize(user_context, block, 
        {alloc.alloc_memory, alloc.free_memory, alloc.alloc_region, alloc.free_region}
    );
    block->allocator = allocator;
    return allocator_entry;
}

void BlockAllocator::destroy_allocator_entry(void* user_context, BlockAllocator::AllocatorList::EntryType* allocator_entry) {
    allocator_list.remove(user_context, allocator_entry);
}

BlockAllocator::BlockList::EntryType* 
BlockAllocator::create_block_entry(void* user_context, MemoryAccess access, size_t size) {
    BlockList::EntryType* block_entry = block_list.append(user_context);
    MemoryBlock* block = &(block_entry->value);
    memset(block, 0, sizeof(MemoryBlock));
    block->size = size;
    block->reserved = 0;
    block->allocator = nullptr;
    block->access = access;
    alloc_block(user_context, block);
    return block_entry;
}

void BlockAllocator::destroy_block_entry(void* user_context, BlockAllocator::BlockList::EntryType* block_entry) {
    MemoryBlock* block = &(block_entry->value);
    free_block(user_context, block);
    block_list.remove(user_context, block_entry);
}

void BlockAllocator::alloc_block(void* user_context, MemoryBlock* block) {
    alloc.alloc_block(user_context, block);
    block->reserved = 0;
}

void BlockAllocator::free_block(void* user_context, MemoryBlock* block) {
    if(block->allocator) {
        block->allocator->destroy(user_context);
        AllocatorList::EntryType* allocator_entry = find_allocator_entry(user_context, block);
        if(allocator_entry != nullptr) {
            destroy_allocator_entry(user_context, allocator_entry);
            block->allocator = nullptr;
        }
    }
    alloc.free_block(user_context, block);
    block->reserved = 0;
    block->size = 0;
}
// --
    
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_BLOCK_ALLOCATOR_H
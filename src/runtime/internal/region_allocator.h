#ifndef HALIDE_RUNTIME_REGION_ALLOCATOR_H
#define HALIDE_RUNTIME_REGION_ALLOCATOR_H

#include "HalideRuntime.h"
#include "memory_arena.h"
#include "memory_resources.h"
#include "printer.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --

/** Allocator class interface for sub-allocating a contiguous
 * memory block into smaller regions of memory. This class only
 * manages the address creation for the regions -- allocation
 * callback functions are used to request the memory from the
 * necessary system or API calls. This class is intended to be
 * used inside of a higher level memory management class that
 * provides thread safety, policy management and API
 * integration for a specific runtime API (eg Vulkan, OpenCL, etc)
 */
class RegionAllocator {
public:
    // disable copy constructors and assignment
    RegionAllocator(const RegionAllocator &) = delete;
    RegionAllocator &operator=(const RegionAllocator &) = delete;

    // disable non-factory based construction
    RegionAllocator() = delete;
    ~RegionAllocator() = delete;

    // Allocators for the different types of memory we need to allocate
    struct MemoryAllocators {
        SystemMemoryAllocatorFns system;
        MemoryRegionAllocatorFns region;
    };

    // Factory methods for creation / destruction
    static RegionAllocator *create(void *user_context, BlockResource *block, const MemoryAllocators &ma);
    static void destroy(void *user_context, RegionAllocator *region_allocator);

    // Returns the allocator class instance for the given allocation (or nullptr)
    static RegionAllocator *find_allocator(void *user_context, MemoryRegion *memory_region);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, const MemoryRequest &request);
    void reclaim(void *user_context, MemoryRegion *memory_region);
    bool collect(void *user_context);  //< returns true if any blocks were removed
    void release(void *user_context);
    void destroy(void *user_context);

    // Returns the currently managed block resource
    BlockResource *block_resource() const;

private:
    // Initializes a new instance
    void initialize(void *user_context, BlockResource *block, const MemoryAllocators &ma);

    // Search through allocated block regions (Best-Fit)
    BlockRegion *find_block_region(void *user_context, const MemoryRequest &request);

    // Returns true if neighbouring block regions to the given region can be coalesced into one
    bool can_coalesce(BlockRegion *region);

    // Merges available neighbouring block regions into the given region
    BlockRegion *coalesce_block_regions(void *user_context, BlockRegion *region);

    // Returns true if the given region can be split to accomadate the given size
    bool can_split(BlockRegion *region, size_t size);

    // Splits the given block region into a smaller region to accomadate the given size, followed by empty space for the remaining
    BlockRegion *split_block_region(void *user_context, BlockRegion *region, size_t size, size_t alignment);

    // Creates a new block region and adds it to the region list
    BlockRegion *create_block_region(void *user_context, const MemoryProperties &properties, size_t offset, size_t size, bool dedicated);

    // Creates a new block region and adds it to the region list
    void destroy_block_region(void *user_context, BlockRegion *region);

    // Invokes the allocation callback to allocate memory for the block region
    void alloc_block_region(void *user_context, BlockRegion *region);

    // Releases a block region and leaves it in the list for further allocations
    void release_block_region(void *user_context, BlockRegion *region);

    // Invokes the deallocation callback to free memory for the block region
    void free_block_region(void *user_context, BlockRegion *region);

    // Returns true if the given block region is compatible with the given properties
    bool is_compatible_block_region(const BlockRegion *region, const MemoryProperties &properties) const;

    BlockResource *block = nullptr;
    MemoryArena *arena = nullptr;
    MemoryAllocators allocators;
};

RegionAllocator *RegionAllocator::create(void *user_context, BlockResource *block_resource, const MemoryAllocators &allocators) {
    halide_debug_assert(user_context, allocators.system.allocate != nullptr);
    RegionAllocator *result = reinterpret_cast<RegionAllocator *>(
        allocators.system.allocate(user_context, sizeof(RegionAllocator)));

    if (result == nullptr) {
        halide_error(user_context, "RegionAllocator: Failed to create instance! Out of memory!\n");
        return nullptr;
    }

    result->initialize(user_context, block_resource, allocators);
    return result;
}

void RegionAllocator::destroy(void *user_context, RegionAllocator *instance) {
    halide_debug_assert(user_context, instance != nullptr);
    const MemoryAllocators &allocators = instance->allocators;
    instance->destroy(user_context);
    halide_debug_assert(user_context, allocators.system.deallocate != nullptr);
    allocators.system.deallocate(user_context, instance);
}

void RegionAllocator::initialize(void *user_context, BlockResource *mb, const MemoryAllocators &ma) {
    block = mb;
    allocators = ma;
    arena = MemoryArena::create(user_context, {sizeof(BlockRegion), MemoryArena::default_capacity, 0}, allocators.system);
    halide_debug_assert(user_context, arena != nullptr);
    block->allocator = this;
    block->regions = create_block_region(
        user_context,
        block->memory.properties,
        0, block->memory.size,
        block->memory.dedicated);
}

MemoryRegion *RegionAllocator::reserve(void *user_context, const MemoryRequest &request) {
    halide_debug_assert(user_context, request.size > 0);
    size_t remaining = block->memory.size - block->reserved;
    if (remaining < request.size) {
#ifdef DEBUG_RUNTIME
        debug(user_context) << "RegionAllocator: Unable to reserve more memory from block "
                            << "-- requested size (" << (int32_t)(request.size) << " bytes) "
                            << "greater than available (" << (int32_t)(remaining) << " bytes)!\n";
#endif
        return nullptr;
    }

    BlockRegion *block_region = find_block_region(user_context, request);
    if (block_region == nullptr) {
#ifdef DEBUG_RUNTIME
        debug(user_context) << "RegionAllocator: Failed to locate region for requested size ("
                            << (int32_t)(request.size) << " bytes)!\n";
#endif
        return nullptr;
    }

    if (can_split(block_region, request.size)) {
#ifdef DEBUG_RUNTIME
        debug(user_context) << "RegionAllocator: Splitting region of size ( " << (int32_t)(block_region->memory.size) << ") "
                            << "to accomodate requested size (" << (int32_t)(request.size) << " bytes)!\n";
#endif
        split_block_region(user_context, block_region, request.size, request.alignment);
    }

    alloc_block_region(user_context, block_region);
    return reinterpret_cast<MemoryRegion *>(block_region);
}

void RegionAllocator::reclaim(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    halide_debug_assert(user_context, block_region != nullptr);
    halide_debug_assert(user_context, block_region->block_ptr == block);
    free_block_region(user_context, block_region);
    if (can_coalesce(block_region)) {
        block_region = coalesce_block_regions(user_context, block_region);
    }
}

RegionAllocator *RegionAllocator::find_allocator(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    halide_debug_assert(user_context, block_region != nullptr);
    halide_debug_assert(user_context, block_region->block_ptr != nullptr);
    return block_region->block_ptr->allocator;
}

BlockRegion *RegionAllocator::find_block_region(void *user_context, const MemoryRequest &request) {
    BlockRegion *result = nullptr;
    for (BlockRegion *block_region = block->regions; block_region != nullptr; block_region = block_region->next_ptr) {

        if (block_region->status != AllocationStatus::Available) {
            continue;
        }

        // skip incompatible block regions for this request
        if (!is_compatible_block_region(block_region, request.properties)) {
            continue;
        }

        // is the requested size larger than the current region?
        if (request.size > block_region->memory.size) {
            continue;
        }

        size_t actual_size = aligned_size(block_region->memory.offset, request.size, request.alignment);

        // is the adjusted size larger than the current region?
        if (actual_size > block_region->memory.size) {
            continue;
        }

        // will the adjusted size fit within the remaining unallocated space?
        if ((actual_size + block->reserved) < block->memory.size) {
            result = block_region;  // best-fit!
            break;
        }
    }
    return result;
}

bool RegionAllocator::can_coalesce(BlockRegion *block_region) {
    if (block_region == nullptr) {
        return false;
    }
    if (block_region->prev_ptr && (block_region->prev_ptr->status == AllocationStatus::Available)) {
        return true;
    }
    if (block_region->next_ptr && (block_region->next_ptr->status == AllocationStatus::Available)) {
        return true;
    }
    return false;
}

BlockRegion *RegionAllocator::coalesce_block_regions(void *user_context, BlockRegion *block_region) {
    if (block_region->prev_ptr && (block_region->prev_ptr->status == AllocationStatus::Available)) {
        BlockRegion *prev_region = block_region->prev_ptr;

#ifdef DEBUG_RUNTIME
        debug(user_context) << "RegionAllocator: Coalescing "
                            << "previous region (offset=" << (int32_t)prev_region->memory.offset << " size=" << (int32_t)(prev_region->memory.size) << " bytes) "
                            << "into current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes)\n!";
#endif

        prev_region->next_ptr = block_region->next_ptr;
        if (block_region->next_ptr) {
            block_region->next_ptr->prev_ptr = prev_region;
        }
        prev_region->memory.size += block_region->memory.size;
        destroy_block_region(user_context, block_region);
        block_region = prev_region;
    }

    if (block_region->next_ptr && (block_region->next_ptr->status == AllocationStatus::Available)) {
        BlockRegion *next_region = block_region->next_ptr;

#ifdef DEBUG_RUNTIME
        debug(user_context) << "RegionAllocator: Coalescing "
                            << "next region (offset=" << (int32_t)next_region->memory.offset << " size=" << (int32_t)(next_region->memory.size) << " bytes) "
                            << "into current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes)!\n";
#endif

        if (next_region->next_ptr) {
            next_region->next_ptr->prev_ptr = block_region;
        }
        block_region->next_ptr = next_region->next_ptr;
        block_region->memory.size += next_region->memory.size;
        destroy_block_region(user_context, next_region);
    }

    return block_region;
}

bool RegionAllocator::can_split(BlockRegion *block_region, size_t size) {
    return (block_region && (block_region->memory.size > size));
}

BlockRegion *RegionAllocator::split_block_region(void *user_context, BlockRegion *block_region, size_t size, size_t alignment) {
    size_t adjusted_size = aligned_size(block_region->memory.offset, size, alignment);
    size_t adjusted_offset = aligned_offset(block_region->memory.offset, alignment);

    size_t empty_offset = adjusted_offset + size;
    size_t empty_size = block_region->memory.size - adjusted_size;

#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Splitting "
                        << "current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes) "
                        << "to create empty region (offset=" << (int32_t)empty_offset << " size=" << (int32_t)(empty_size) << " bytes)!\n";
#endif

    BlockRegion *next_region = block_region->next_ptr;
    BlockRegion *empty_region = create_block_region(user_context,
                                                    block_region->memory.properties,
                                                    empty_offset, empty_size,
                                                    block_region->memory.dedicated);
    halide_debug_assert(user_context, empty_region != nullptr);

    empty_region->next_ptr = next_region;
    if (next_region) {
        next_region->prev_ptr = empty_region;
    }
    block_region->next_ptr = empty_region;
    block_region->memory.size = size;
    return empty_region;
}

BlockRegion *RegionAllocator::create_block_region(void *user_context, const MemoryProperties &properties, size_t offset, size_t size, bool dedicated) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Creating block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "offset=" << (uint32_t)offset << " "
                        << "size=" << (uint32_t)size << " "
                        << "dedicated=" << (dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(properties.visibility) << ") ...\n";
#endif

    BlockRegion *block_region = static_cast<BlockRegion *>(arena->reserve(user_context, true));

    if (block_region == nullptr) {
        error(user_context) << "RegionAllocator: Failed to allocate new block region!\n";
        return nullptr;
    }

#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Added block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif

    block_region->memory.offset = offset;
    block_region->memory.size = size;
    block_region->memory.properties = properties;
    block_region->memory.dedicated = dedicated;
    block_region->status = AllocationStatus::Available;
    block_region->block_ptr = block;
    return block_region;
}

void RegionAllocator::release_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Releasing block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif
    block_region->status = AllocationStatus::Available;
}

void RegionAllocator::destroy_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Destroying block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif

    free_block_region(user_context, block_region);
    arena->reclaim(user_context, block_region);
}

void RegionAllocator::alloc_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Allocating region (size=" << (int32_t)(block_region->memory.size) << ", offset=" << (int32_t)block_region->memory.offset << ")!\n";
#endif
    halide_debug_assert(user_context, allocators.region.allocate != nullptr);
    halide_debug_assert(user_context, block_region->status == AllocationStatus::Available);
    MemoryRegion *memory_region = &(block_region->memory);
    allocators.region.allocate(user_context, memory_region);
    block_region->status = block_region->memory.dedicated ? AllocationStatus::Dedicated : AllocationStatus::InUse;
    block->reserved += block_region->memory.size;
}

void RegionAllocator::free_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Freeing block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif
    if ((block_region->status == AllocationStatus::InUse) ||
        (block_region->status == AllocationStatus::Dedicated)) {
        debug(user_context) << "RegionAllocator: Deallocating region (size=" << (int32_t)(block_region->memory.size) << ", offset=" << (int32_t)block_region->memory.offset << ")!\n";
        halide_debug_assert(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block->reserved -= block_region->memory.size;
        block_region->memory.size = 0;
    }
    block_region->status = AllocationStatus::Available;
}

void RegionAllocator::release(void *user_context) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Releasing all regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";
#endif
    for (BlockRegion *block_region = block->regions; block_region != nullptr; block_region = block_region->next_ptr) {
        release_block_region(user_context, block_region);
    }
}

bool RegionAllocator::collect(void *user_context) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Collecting free block regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";
#endif
    bool result = false;
    for (BlockRegion *block_region = block->regions; block_region != nullptr; block_region = block_region->next_ptr) {
        if (block_region->status == AllocationStatus::Available) {
            if (can_coalesce(block_region)) {
                block_region = coalesce_block_regions(user_context, block_region);
                result = true;
            }
        }
    }
    return result;
}

void RegionAllocator::destroy(void *user_context) {
#ifdef DEBUG_RUNTIME
    debug(user_context) << "RegionAllocator: Destroying all block regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";
#endif
    for (BlockRegion *block_region = block->regions; block_region != nullptr;) {

        if (block_region->next_ptr == nullptr) {
            destroy_block_region(user_context, block_region);
            block_region = nullptr;
        } else {
            BlockRegion *prev_region = block_region;
            block_region = block_region->next_ptr;
            destroy_block_region(user_context, prev_region);
        }
    }
    block->reserved = 0;
    block->regions = nullptr;
    block->allocator = nullptr;
    MemoryArena::destroy(user_context, arena);
    arena = nullptr;
}

bool RegionAllocator::is_compatible_block_region(const BlockRegion *block_region, const MemoryProperties &properties) const {
    if (properties.caching != MemoryCaching::DefaultCaching) {
        if (properties.caching != block_region->memory.properties.caching) {
            return false;
        }
    }

    if (properties.visibility != MemoryVisibility::DefaultVisibility) {
        if (properties.visibility != block_region->memory.properties.visibility) {
            return false;
        }
    }

    if (properties.usage != MemoryUsage::DefaultUsage) {
        if (properties.usage != block_region->memory.properties.usage) {
            return false;
        }
    }

    return true;
}

BlockResource *RegionAllocator::block_resource() const {
    return block;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_REGION_ALLOCATOR_H

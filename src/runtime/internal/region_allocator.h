#ifndef HALIDE_RUNTIME_REGION_ALLOCATOR_H
#define HALIDE_RUNTIME_REGION_ALLOCATOR_H

#include "../HalideRuntime.h"
#include "../printer.h"
#include "memory_arena.h"
#include "memory_resources.h"

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
    static int destroy(void *user_context, RegionAllocator *region_allocator);

    // Returns the allocator class instance for the given allocation (or nullptr)
    static RegionAllocator *find_allocator(void *user_context, MemoryRegion *memory_region);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, const MemoryRequest &request);
    int release(void *user_context, MemoryRegion *memory_region);  //< unmark and cache the region for reuse
    int reclaim(void *user_context, MemoryRegion *memory_region);  //< free the region and consolidate
    int retain(void *user_context, MemoryRegion *memory_region);   //< retain the region and increase usage count
    bool collect(void *user_context);                              //< returns true if any blocks were removed
    int release(void *user_context);
    int destroy(void *user_context);

    // Returns the currently managed block resource
    BlockResource *block_resource() const;

private:
    // Initializes a new instance
    int initialize(void *user_context, BlockResource *block, const MemoryAllocators &ma);

    // Search through allocated block regions (Best-Fit)
    BlockRegion *find_block_region(void *user_context, const MemoryRequest &request);

    // Returns true if block region is unused and available
    bool is_available(const BlockRegion *region) const;

    // Returns true if neighbouring block regions to the given region can be coalesced into one
    bool can_coalesce(const BlockRegion *region) const;

    // Merges available neighbouring block regions into the given region
    BlockRegion *coalesce_block_regions(void *user_context, BlockRegion *region);

    // Returns true if the given region can be split to accomodate the given size
    bool can_split(const BlockRegion *region, size_t size) const;

    // Splits the given block region into a smaller region to accomodate the given size, followed by empty space for the remaining
    BlockRegion *split_block_region(void *user_context, BlockRegion *region, size_t size, size_t alignment);

    // Creates a new block region and adds it to the region list
    BlockRegion *create_block_region(void *user_context, const MemoryProperties &properties, size_t offset, size_t size, bool dedicated);

    // Creates a new block region and adds it to the region list
    int destroy_block_region(void *user_context, BlockRegion *region);

    // Invokes the allocation callback to allocate memory for the block region
    int alloc_block_region(void *user_context, BlockRegion *region);

    // Releases a block region and leaves it in the list for further allocations
    int release_block_region(void *user_context, BlockRegion *region);

    // Invokes the deallocation callback to free memory for the block region
    int free_block_region(void *user_context, BlockRegion *region);

    // Returns true if the given block region is the last region in the list
    bool is_last_block_region(void *user_context, const BlockRegion *region) const;

    // Returns true if the given block region is compatible with the given properties
    bool is_compatible_block_region(const BlockRegion *region, const MemoryProperties &properties) const;

    // Returns true if the given block region is suitable for the requested allocation
    bool is_block_region_suitable_for_request(void *user_context, const BlockRegion *region, const MemoryRequest &request) const;

    // Returns the number of active regions for the block;
    size_t region_count(void *user_context) const;

    BlockResource *block = nullptr;
    MemoryArena *arena = nullptr;
    MemoryAllocators allocators;
};

RegionAllocator *RegionAllocator::create(void *user_context, BlockResource *block_resource, const MemoryAllocators &allocators) {
    halide_abort_if_false(user_context, allocators.system.allocate != nullptr);
    RegionAllocator *result = reinterpret_cast<RegionAllocator *>(
        allocators.system.allocate(user_context, sizeof(RegionAllocator)));

    if (result == nullptr) {
        return nullptr;
    }

    result->initialize(user_context, block_resource, allocators);
    return result;
}

int RegionAllocator::destroy(void *user_context, RegionAllocator *instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    const MemoryAllocators &allocators = instance->allocators;
    instance->destroy(user_context);
    halide_abort_if_false(user_context, allocators.system.deallocate != nullptr);
    allocators.system.deallocate(user_context, instance);
    return 0;
}

int RegionAllocator::initialize(void *user_context, BlockResource *mb, const MemoryAllocators &ma) {
    block = mb;
    allocators = ma;
    arena = MemoryArena::create(user_context, {sizeof(BlockRegion), MemoryArena::default_capacity, 0}, allocators.system);
    halide_abort_if_false(user_context, arena != nullptr);
    block->allocator = this;
    block->regions = create_block_region(
        user_context,
        block->memory.properties,
        0, block->memory.size,
        block->memory.dedicated);
    return 0;
}

MemoryRegion *RegionAllocator::reserve(void *user_context, const MemoryRequest &request) {
    halide_abort_if_false(user_context, request.size > 0);
    size_t actual_alignment = conform_alignment(request.alignment, block->memory.properties.alignment);
    size_t actual_size = conform_size(request.offset, request.size, actual_alignment, block->memory.properties.nearest_multiple);
    size_t remaining = block->memory.size - block->reserved;
    if (remaining < actual_size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Unable to reserve more memory from block "
                            << "-- requested size (" << (int32_t)(request.size) << " bytes) "
                            << "greater than available (" << (int32_t)(remaining) << " bytes)!\n";
#endif
        return nullptr;
    }

    BlockRegion *block_region = find_block_region(user_context, request);
    if (block_region == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to locate region for requested size ("
                            << (int32_t)(request.size) << " bytes)!\n";
#endif
        return nullptr;
    }

    if (can_split(block_region, request.size)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Splitting region of size ( " << (int32_t)(block_region->memory.size) << ") "
                            << "to accomodate requested size (" << (int32_t)(request.size) << " bytes)!\n";
#endif
        split_block_region(user_context, block_region, request.size, request.alignment);
    }

    alloc_block_region(user_context, block_region);
    return reinterpret_cast<MemoryRegion *>(block_region);
}

int RegionAllocator::release(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    halide_abort_if_false(user_context, block_region != nullptr);
    halide_abort_if_false(user_context, block_region->block_ptr == block);
    if (block_region->usage_count > 0) {
        block_region->usage_count--;
    }
    return release_block_region(user_context, block_region);
}

int RegionAllocator::reclaim(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    halide_abort_if_false(user_context, block_region != nullptr);
    halide_abort_if_false(user_context, block_region->block_ptr == block);
    if (block_region->usage_count > 0) {
        block_region->usage_count--;
    }
    release_block_region(user_context, block_region);
    free_block_region(user_context, block_region);
    if (can_coalesce(block_region)) {
        block_region = coalesce_block_regions(user_context, block_region);
    }
    return 0;
}

int RegionAllocator::retain(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    halide_abort_if_false(user_context, block_region != nullptr);
    halide_abort_if_false(user_context, block_region->block_ptr == block);
    block_region->usage_count++;
    return 0;
}

RegionAllocator *RegionAllocator::find_allocator(void *user_context, MemoryRegion *memory_region) {
    BlockRegion *block_region = reinterpret_cast<BlockRegion *>(memory_region);
    if (block_region == nullptr) {
        return nullptr;
    }
    if (block_region->block_ptr == nullptr) {
        return nullptr;
    }
    return block_region->block_ptr->allocator;
}

bool RegionAllocator::is_last_block_region(void *user_context, const BlockRegion *region) const {
    return ((region == nullptr) || (region == region->next_ptr) || (region->next_ptr == nullptr));
}

bool RegionAllocator::is_block_region_suitable_for_request(void *user_context, const BlockRegion *region, const MemoryRequest &request) const {
    if (!is_available(region)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: skipping block region ... not available! "
                            << " block_region=" << (void *)region << "\n";
#endif
        return false;
    }

    // skip incompatible block regions for this request
    if (!is_compatible_block_region(region, request.properties)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: skipping block region ... incompatible properties! "
                            << " block_region=" << (void *)region << "\n";
#endif
        return false;
    }

    size_t actual_alignment = conform_alignment(request.alignment, block->memory.properties.alignment);
    size_t actual_size = conform_size(region->memory.offset, request.size, actual_alignment, block->memory.properties.nearest_multiple);

    // is the adjusted size larger than the current region?
    if (actual_size > region->memory.size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: skipping block region ... not enough space for adjusted size! "
                            << " block_region=" << (void *)region << "\n";
#endif
        return false;
    }

    // will the adjusted size fit within the remaining unallocated space?
    if ((actual_size + block->reserved) <= block->memory.size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: found suitable block region! "
                            << " block_region=" << (void *)region << "\n";
#endif
        return true;  // you betcha
    }

    return false;
}

BlockRegion *RegionAllocator::find_block_region(void *user_context, const MemoryRequest &request) {
    BlockRegion *block_region = block->regions;
    while (block_region != nullptr) {
        if (is_block_region_suitable_for_request(user_context, block_region, request)) {
#ifdef DEBUG_RUNTIME_INTERNAL
            debug(user_context) << "RegionAllocator: found suitable region ...\n"
                                << " user_context=" << (void *)(user_context) << "\n"
                                << " block_resource=" << (void *)block << "\n"
                                << " block_size=" << (uint32_t)block->memory.size << "\n"
                                << " block_reserved=" << (uint32_t)block->reserved << "\n"
                                << " requested_size=" << (uint32_t)request.size << "\n"
                                << " requested_is_dedicated=" << (request.dedicated ? "true" : "false") << "\n"
                                << " requested_usage=" << halide_memory_usage_name(request.properties.usage) << "\n"
                                << " requested_caching=" << halide_memory_caching_name(request.properties.caching) << "\n"
                                << " requested_visibility=" << halide_memory_visibility_name(request.properties.visibility) << "\n";
#endif
            return block_region;
        }

        if (is_last_block_region(user_context, block_region)) {
            block_region = nullptr;  // end of list ... nothing found
            break;
        }
        block_region = block_region->next_ptr;
    }

    if (block_region == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: couldn't find suitable region!\n"
                            << " user_context=" << (void *)(user_context) << "\n"
                            << " requested_size=" << (uint32_t)request.size << "\n"
                            << " requested_is_dedicated=" << (request.dedicated ? "true" : "false") << "\n"
                            << " requested_usage=" << halide_memory_usage_name(request.properties.usage) << "\n"
                            << " requested_caching=" << halide_memory_caching_name(request.properties.caching) << "\n"
                            << " requested_visibility=" << halide_memory_visibility_name(request.properties.visibility) << "\n";
#endif
    }

    return block_region;
}

bool RegionAllocator::is_available(const BlockRegion *block_region) const {
    if (block_region == nullptr) {
        return false;
    }
    if (block_region->usage_count > 0) {
        return false;
    }
    if (block_region->status != AllocationStatus::Available) {
        return false;
    }
    return true;
}

bool RegionAllocator::can_coalesce(const BlockRegion *block_region) const {
    if (!is_available(block_region)) {
        return false;
    }
    if (is_available(block_region->prev_ptr)) {
        return true;
    }
    if (is_available(block_region->next_ptr)) {
        return true;
    }
    return false;
}

BlockRegion *RegionAllocator::coalesce_block_regions(void *user_context, BlockRegion *block_region) {

    if ((block_region->usage_count == 0) && (block_region->memory.handle != nullptr)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "Freeing region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")\n";
#endif
        halide_abort_if_false(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block_region->memory.handle = nullptr;
    }

    BlockRegion *prev_region = block_region->prev_ptr;
    if (is_available(prev_region) && (prev_region != block_region)) {

#ifdef DEBUG_RUNTIME_INTERNAL
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

    BlockRegion *next_region = block_region->next_ptr;
    if (is_available(next_region) && (next_region != block_region)) {

#ifdef DEBUG_RUNTIME_INTERNAL
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

bool RegionAllocator::can_split(const BlockRegion *block_region, size_t size) const {
    return (block_region && (block_region->memory.size > size) && (block_region->usage_count == 0));
}

BlockRegion *RegionAllocator::split_block_region(void *user_context, BlockRegion *block_region, size_t size, size_t alignment) {

    if ((block_region->usage_count == 0) && (block_region->memory.handle != nullptr)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Split deallocate region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block_region->block_ptr->reserved << " "
                            << ")\n";
#endif
        halide_abort_if_false(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block_region->memory.handle = nullptr;
    }

    size_t actual_alignment = conform_alignment(alignment, block->memory.properties.alignment);
    size_t actual_size = conform_size(block_region->memory.offset, size, actual_alignment, block->memory.properties.nearest_multiple);
    size_t actual_offset = aligned_offset(block_region->memory.offset + size, actual_alignment);
    size_t empty_size = block_region->memory.size - actual_size;

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Conforming size and alignment \n"
                        << " requested_size=" << (uint32_t)size << "\n"
                        << " actual_size=" << (uint32_t)actual_size << "\n"
                        << " requested_alignment=" << (uint32_t)alignment << " "
                        << " required_alignment=" << (uint32_t)block->memory.properties.alignment << " "
                        << " actual_alignment=" << (uint32_t)actual_alignment << ")\n";
#endif

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Splitting "
                        << "current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes) "
                        << "to create empty region (offset=" << (int32_t)actual_offset << " size=" << (int32_t)(empty_size) << " bytes)!\n";
#endif

    BlockRegion *next_region = block_region->next_ptr;
    BlockRegion *empty_region = create_block_region(user_context,
                                                    block_region->memory.properties,
                                                    actual_offset, empty_size,
                                                    block_region->memory.dedicated);
    halide_abort_if_false(user_context, empty_region != nullptr);

    empty_region->next_ptr = next_region;
    if (next_region) {
        next_region->prev_ptr = empty_region;
    }
    empty_region->prev_ptr = block_region;
    block_region->next_ptr = empty_region;
    block_region->memory.size -= empty_size;
    return empty_region;
}

BlockRegion *RegionAllocator::create_block_region(void *user_context, const MemoryProperties &properties, size_t offset, size_t size, bool dedicated) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Creating block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "offset=" << (uint32_t)offset << " "
                        << "size=" << (uint32_t)size << " "
                        << "alignment=" << (uint32_t)properties.alignment << " "
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

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Added block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif

    block_region->memory.handle = nullptr;
    block_region->memory.offset = offset;
    block_region->memory.size = size;
    block_region->memory.properties = properties;
    block_region->memory.dedicated = dedicated;
    block_region->status = AllocationStatus::Available;
    block_region->block_ptr = block;
    block_region->usage_count = 0;

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "Creating region ("
                        << "block_ptr=" << (void *)block_region->block_ptr << " "
                        << "block_region=" << (void *)block_region << " "
                        << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                        << ")\n";
#endif

    return block_region;
}

int RegionAllocator::release_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Releasing block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif
    if (block_region == nullptr) {
        return 0;
    }

    if (block_region->usage_count > 0) {
        return 0;
    }

    if (block_region->status != AllocationStatus::Available) {

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "Releasing region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)(block->reserved - block_region->memory.size) << " "
                            << ")\n";
#endif

        block->reserved -= block_region->memory.size;
    }
    block_region->status = AllocationStatus::Available;
    return 0;
}

int RegionAllocator::destroy_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Destroying block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << ") ...\n";
#endif

    block_region->usage_count = 0;
    release_block_region(user_context, block_region);
    free_block_region(user_context, block_region);
    arena->reclaim(user_context, block_region);
    return 0;
}

int RegionAllocator::alloc_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Allocating region (user_context=" << (void *)(user_context)
                        << " size=" << (int32_t)(block_region->memory.size)
                        << " offset=" << (int32_t)block_region->memory.offset << ")!\n";
#endif
    halide_abort_if_false(user_context, allocators.region.allocate != nullptr);
    halide_abort_if_false(user_context, block_region->status == AllocationStatus::Available);
    int error_code = 0;
    MemoryRegion *memory_region = &(block_region->memory);
    if (memory_region->handle == nullptr) {
        error_code = allocators.region.allocate(user_context, memory_region);
        memory_region->is_owner = true;

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "Allocating region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")\n";
#endif

    } else {

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "Re-using region  ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")\n";
#endif
    }
    block_region->status = block_region->memory.dedicated ? AllocationStatus::Dedicated : AllocationStatus::InUse;
    block->reserved += block_region->memory.size;
    return error_code;
}

int RegionAllocator::free_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Freeing block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_region=" << (void *)(block_region) << " "
                        << "status=" << (uint32_t)block_region->status << " "
                        << "usage_count=" << (uint32_t)block_region->usage_count << ") ...\n";
#endif
    if ((block_region->usage_count == 0) && (block_region->memory.handle != nullptr)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "Freeing region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")\n";
#endif
        halide_abort_if_false(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block_region->memory.size = 0;
        block_region->memory.offset = 0;
        block_region->memory.handle = nullptr;
    }
    block_region->usage_count = 0;
    block_region->status = AllocationStatus::Available;
    return 0;
}

int RegionAllocator::release(void *user_context) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Releasing all regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";
#endif

    BlockRegion *block_region = block->regions;
    while (block_region != nullptr) {
        release_block_region(user_context, block_region);
        if (is_last_block_region(user_context, block_region)) {
            break;
        }
        block_region = block_region->next_ptr;
    }
    return 0;
}

bool RegionAllocator::collect(void *user_context) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Collecting free block regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";

    uint32_t count = 0;
    uint64_t reserved = block->reserved;
    debug(user_context) << "    collecting unused regions ("
                        << "block_ptr=" << (void *)block << " "
                        << "block_reserved=" << (uint32_t)block->reserved << " "
                        << ")\n";
#endif

    bool has_collected = false;
    BlockRegion *block_region = block->regions;
    while (block_region != nullptr) {
        if (can_coalesce(block_region)) {
#ifdef DEBUG_RUNTIME_INTERNAL
            count++;
            debug(user_context) << "    collecting region ("
                                << "block_ptr=" << (void *)block_region->block_ptr << " "
                                << "block_region=" << (void *)block_region << " "
                                << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                                << "block_reserved=" << (uint32_t)block->reserved << " "
                                << ")\n";
#endif
            block_region = coalesce_block_regions(user_context, block_region);
            has_collected = true;
        }
        if (is_last_block_region(user_context, block_region)) {
            break;
        }
        block_region = block_region->next_ptr;
    }

    if (has_collected) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    collected unused regions ("
                            << "block_ptr=" << (void *)block << " "
                            << "region_count=" << (uint32_t)count << " "
                            << "collected=" << (uint32_t)(reserved - block->reserved) << " "
                            << ")\n";
#endif
    }
    return has_collected;
}

int RegionAllocator::destroy(void *user_context) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Destroying all block regions ("
                        << "user_context=" << (void *)(user_context) << ") ...\n";
#endif
    for (BlockRegion *block_region = block->regions; block_region != nullptr;) {

        if (is_last_block_region(user_context, block_region)) {
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
    return 0;
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

size_t RegionAllocator::region_count(void *user_context) const {
    if (block == nullptr) {
        return 0;
    }
    size_t count = 0;
    for (BlockRegion const *region = block->regions; !is_last_block_region(user_context, region); region = region->next_ptr) {
        ++count;
    }
    return count;
}

BlockResource *RegionAllocator::block_resource() const {
    return block;
}

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_REGION_ALLOCATOR_H

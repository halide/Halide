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
    int conform(void *user_context, MemoryRequest *request) const;  //< conform the given request into a suitable allocation
    int release(void *user_context, MemoryRegion *memory_region);   //< unmark and cache the region for reuse
    int reclaim(void *user_context, MemoryRegion *memory_region);   //< free the region and consolidate
    int retain(void *user_context, MemoryRegion *memory_region);    //< retain the region and increase usage count
    bool collect(void *user_context);                               //< returns true if any blocks were removed
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
    bool can_split(const BlockRegion *region, const MemoryRequest &request) const;

    // Splits the given block region into a smaller region to accomodate the given size, followed by empty space for the remaining
    BlockRegion *split_block_region(void *user_context, BlockRegion *region, const MemoryRequest &request);

    // Creates a new block region and adds it to the region list
    BlockRegion *create_block_region(void *user_context, const MemoryRequest &request);

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
    MemoryAllocators allocators = instance->allocators;
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
    MemoryRequest block_request = {};
    block_request.size = block->memory.size;
    block_request.offset = 0;
    block_request.alignment = block->memory.properties.alignment;
    block_request.properties = block->memory.properties;
    block_request.dedicated = block->memory.dedicated;
    block->allocator = this;
    block->regions = create_block_region(user_context, block_request);
    return 0;
}

int RegionAllocator::conform(void *user_context, MemoryRequest *request) const {
    if (allocators.region.conform) {
        return allocators.region.conform(user_context, request);
    } else {
        size_t actual_alignment = conform_alignment(request->alignment, block->memory.properties.alignment);
        size_t actual_offset = aligned_offset(request->offset, actual_alignment);
        size_t actual_size = conform_size(actual_offset, request->size, actual_alignment, block->memory.properties.nearest_multiple);
        request->alignment = actual_alignment;
        request->offset = actual_offset;
        request->size = actual_size;
    }
    return 0;
}

MemoryRegion *RegionAllocator::reserve(void *user_context, const MemoryRequest &request) {
    halide_abort_if_false(user_context, request.size > 0);

    MemoryRequest region_request = request;

    int error_code = conform(user_context, &region_request);
    if (error_code) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to conform region request! Unable to reserve memory ...\n";
#endif
        return nullptr;
    }

    size_t remaining = block->memory.size - block->reserved;
    if (remaining < region_request.size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Unable to reserve more memory from block "
                            << "-- requested size (" << (int32_t)(region_request.size) << " bytes) "
                            << "greater than available (" << (int32_t)(remaining) << " bytes)";
#endif
        return nullptr;
    }

    BlockRegion *block_region = find_block_region(user_context, region_request);
    if (block_region == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to locate region for requested size ("
                            << (int32_t)(request.size) << " bytes)";
#endif
        return nullptr;
    }

    if (can_split(block_region, region_request)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Splitting region of size ( " << (int32_t)(block_region->memory.size) << ") "
                            << "to accomodate requested size (" << (int32_t)(region_request.size) << " bytes)";
#endif
        split_block_region(user_context, block_region, region_request);
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
        debug(user_context) << "    skipping block region ... not available! ("
                            << " block_region=" << (void *)region
                            << " region_size=" << (uint32_t)(region->memory.size)
                            << ")";
#endif
        return false;
    }

    MemoryRequest region_request = request;
    int error_code = conform(user_context, &region_request);
    if (error_code) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to conform region request! Unable to reserve memory ...\n";
#endif
        return false;
    }

    // skip incompatible block regions for this request
    if (!is_compatible_block_region(region, region_request.properties)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    skipping block region ... incompatible properties! ("
                            << " block_region=" << (void *)region
                            << " region_size=" << (uint32_t)(region->memory.size)
                            << ")";
#endif
        return false;
    }

    // is the adjusted size larger than the current region?
    if (region_request.size > region->memory.size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    skipping block region ... not enough space for adjusted size! ("
                            << " block_region=" << (void *)region
                            << " request_size=" << (uint32_t)(request.size)
                            << " actual_size=" << (uint32_t)(region_request.size)
                            << " region_size=" << (uint32_t)(region->memory.size)
                            << ")";
#endif
        return false;
    }

    // will the adjusted size fit within the remaining unallocated space?
    if ((region_request.size + block->reserved) <= block->memory.size) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    found suitable block region! ("
                            << " block_region=" << (void *)region
                            << " request_size=" << (uint32_t)(request.size)
                            << " actual_size=" << (uint32_t)(region_request.size)
                            << " region_size=" << (uint32_t)(region->memory.size)
                            << ")";
#endif
        return true;  // you betcha
    }

    return false;
}

BlockRegion *RegionAllocator::find_block_region(void *user_context, const MemoryRequest &request) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: find block region ( "
                        << "user_context=" << (void *)(user_context) << " "
                        << "requested_size=" << (uint32_t)request.size << " "
                        << "requested_is_dedicated=" << (request.dedicated ? "true" : "false") << " "
                        << "requested_usage=" << halide_memory_usage_name(request.properties.usage) << " "
                        << "requested_caching=" << halide_memory_caching_name(request.properties.caching) << " "
                        << "requested_visibility=" << halide_memory_visibility_name(request.properties.visibility) << ")";
#endif
    BlockRegion *block_region = block->regions;
    while (block_region != nullptr) {
        if (is_block_region_suitable_for_request(user_context, block_region, request)) {
#ifdef DEBUG_RUNTIME_INTERNAL
            debug(user_context) << "RegionAllocator: found suitable region ( "
                                << "user_context=" << (void *)(user_context) << " "
                                << "block_resource=" << (void *)block << " "
                                << "block_size=" << (uint32_t)block->memory.size << " "
                                << "block_reserved=" << (uint32_t)block->reserved << " "
                                << "requested_size=" << (uint32_t)request.size << " "
                                << "requested_is_dedicated=" << (request.dedicated ? "true" : "false") << " "
                                << "requested_usage=" << halide_memory_usage_name(request.properties.usage) << " "
                                << "requested_caching=" << halide_memory_caching_name(request.properties.caching) << " "
                                << "requested_visibility=" << halide_memory_visibility_name(request.properties.visibility) << ")";
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
        debug(user_context) << "RegionAllocator: couldn't find suitable region! ("
                            << "user_context=" << (void *)(user_context) << " "
                            << "requested_size=" << (uint32_t)request.size << " "
                            << "requested_is_dedicated=" << (request.dedicated ? "true" : "false") << " "
                            << "requested_usage=" << halide_memory_usage_name(request.properties.usage) << " "
                            << "requested_caching=" << halide_memory_caching_name(request.properties.caching) << " "
                            << "requested_visibility=" << halide_memory_visibility_name(request.properties.visibility) << ")";
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
        debug(user_context) << "RegionAllocator: Freeing unused region to coalesce ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")";
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
                            << "into current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes)!";
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
                            << "into current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes)";
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

bool RegionAllocator::can_split(const BlockRegion *block_region, const MemoryRequest &split_request) const {
    return (block_region && (block_region->memory.size > split_request.size) && (block_region->usage_count == 0));
}

BlockRegion *RegionAllocator::split_block_region(void *user_context, BlockRegion *block_region, const MemoryRequest &request) {

    if ((block_region->usage_count == 0) && (block_region->memory.handle != nullptr)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Split deallocate region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block_region->block_ptr->reserved << " "
                            << ")";
#endif
        halide_abort_if_false(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block_region->memory.handle = nullptr;
    }

    MemoryRequest split_request = request;
    split_request.size = block_region->memory.size - request.size;
    split_request.offset = block_region->memory.offset + request.size;

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Splitting "
                        << "current region (offset=" << (int32_t)block_region->memory.offset << " size=" << (int32_t)(block_region->memory.size) << " bytes) "
                        << "to create empty region (offset=" << (int32_t)split_request.offset << " size=" << (int32_t)(split_request.size) << " bytes)";
#endif
    BlockRegion *next_region = block_region->next_ptr;
    BlockRegion *empty_region = create_block_region(user_context, split_request);
    halide_abort_if_false(user_context, empty_region != nullptr);

    empty_region->next_ptr = next_region;
    if (next_region) {
        next_region->prev_ptr = empty_region;
    }
    empty_region->prev_ptr = block_region;
    block_region->next_ptr = empty_region;
    block_region->memory.size -= empty_region->memory.size;
    return empty_region;
}

BlockRegion *RegionAllocator::create_block_region(void *user_context, const MemoryRequest &request) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Creating block region request ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "offset=" << (uint32_t)request.offset << " "
                        << "size=" << (uint32_t)request.size << " "
                        << "alignment=" << (uint32_t)request.properties.alignment << " "
                        << "dedicated=" << (request.dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(request.properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(request.properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(request.properties.visibility) << ") ...";
#endif

    MemoryRequest region_request = request;
    int error_code = conform(user_context, &region_request);
    if (error_code) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to conform request for new block region!\n";
#endif
        return nullptr;
    }

    if (region_request.size == 0) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to allocate new block region ... region size was zero!\n";
#endif
        return nullptr;
    }

    BlockRegion *block_region = static_cast<BlockRegion *>(arena->reserve(user_context, true));
    if (block_region == nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "RegionAllocator: Failed to allocate new block region!\n";
#endif
        return nullptr;
    }

    block_region->memory.handle = nullptr;
    block_region->memory.offset = region_request.offset;
    block_region->memory.size = region_request.size;
    block_region->memory.properties = region_request.properties;
    block_region->memory.dedicated = region_request.dedicated;
    block_region->status = AllocationStatus::Available;
    block_region->block_ptr = block;
    block_region->usage_count = 0;

#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Created block region allocation ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_ptr=" << (void *)block_region->block_ptr << " "
                        << "block_region=" << (void *)block_region << " "
                        << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                        << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                        << ")";
#endif

    return block_region;
}

int RegionAllocator::release_block_region(void *user_context, BlockRegion *block_region) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Releasing block region ("
                        << "user_context=" << (void *)(user_context) << " "
                        << "block_ptr=" << ((block_region) ? ((void *)block_region->block_ptr) : nullptr) << " "
                        << "block_region=" << (void *)block_region << " "
                        << "usage_count=" << ((block_region) ? (uint32_t)(block_region->usage_count) : 0) << " "
                        << "memory_offset=" << ((block_region) ? (uint32_t)(block_region->memory.offset) : 0) << " "
                        << "memory_size=" << ((block_region) ? (uint32_t)(block_region->memory.size) : 0) << " "
                        << "block_reserved=" << (uint32_t)(block->reserved) << ") ... ";
#endif
    if (block_region == nullptr) {
        return 0;
    }

    if (block_region->usage_count > 0) {
        return 0;
    }

    if (block_region->status != AllocationStatus::Available) {

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    releasing region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)(block->reserved - block_region->memory.size) << " "
                            << ")";
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
                        << "block_region=" << (void *)(block_region) << ") ...";
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
                        << " offset=" << (int32_t)block_region->memory.offset << ")";
#endif
    halide_abort_if_false(user_context, allocators.region.allocate != nullptr);
    halide_abort_if_false(user_context, block_region->status == AllocationStatus::Available);
    int error_code = 0;
    MemoryRegion *memory_region = &(block_region->memory);
    if (memory_region->handle == nullptr) {
        error_code = allocators.region.allocate(user_context, memory_region);
        memory_region->is_owner = true;

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    allocating region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")";
#endif

    } else {

#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    re-using region  ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_offset=" << (uint32_t)(block_region->memory.offset) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")";
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
                        << "block_ptr=" << (void *)block_region->block_ptr << " "
                        << "block_region=" << (void *)(block_region) << " "
                        << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                        << "status=" << (uint32_t)block_region->status << " "
                        << "usage_count=" << (uint32_t)block_region->usage_count << " "
                        << "block_reserved=" << (uint32_t)block->reserved << ")";
#endif
    if ((block_region->usage_count == 0) && (block_region->memory.handle != nullptr)) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    deallocating region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")";
#endif
        // NOTE: Deallocate but leave memory size as is, so that coalesce can compute region merging sizes
        halide_abort_if_false(user_context, allocators.region.deallocate != nullptr);
        MemoryRegion *memory_region = &(block_region->memory);
        allocators.region.deallocate(user_context, memory_region);
        block_region->memory.handle = nullptr;
    }
    block_region->usage_count = 0;
    block_region->status = AllocationStatus::Available;
    return 0;
}

int RegionAllocator::release(void *user_context) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Releasing all regions ("
                        << "user_context=" << (void *)(user_context) << ") ...";
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
                        << "user_context=" << (void *)(user_context) << ") ...";

    uint32_t collected_count = 0;
    uint32_t remaining_count = 0;
    uint64_t available_bytes = 0;
    uint64_t scanned_bytes = 0;
    uint64_t reserved = block->reserved;
    debug(user_context) << "    collecting unused regions ("
                        << "block_ptr=" << (void *)block << " "
                        << "block_reserved=" << (uint32_t)block->reserved << " "
                        << ")";
#endif

    bool has_collected = false;
    BlockRegion *block_region = block->regions;
    while (block_region != nullptr) {
#ifdef DEBUG_RUNTIME_INTERNAL
        scanned_bytes += block_region->memory.size;
        debug(user_context) << "    checking region ("
                            << "block_ptr=" << (void *)block_region->block_ptr << " "
                            << "block_region=" << (void *)block_region << " "
                            << "usage_count=" << (uint32_t)(block_region->usage_count) << " "
                            << "status=" << (uint32_t)(block_region->status) << " "
                            << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                            << "block_reserved=" << (uint32_t)block->reserved << " "
                            << ")";
#endif

        if (can_coalesce(block_region)) {
#ifdef DEBUG_RUNTIME_INTERNAL
            collected_count++;
            debug(user_context) << "    collecting region ("
                                << "block_ptr=" << (void *)block_region->block_ptr << " "
                                << "block_region=" << (void *)block_region << " "
                                << "memory_size=" << (uint32_t)(block_region->memory.size) << " "
                                << "block_reserved=" << (uint32_t)block->reserved << " "
                                << ")";
#endif
            block_region = coalesce_block_regions(user_context, block_region);
            has_collected = true;
        } else {
#ifdef DEBUG_RUNTIME_INTERNAL
            remaining_count++;
#endif
        }
#ifdef DEBUG_RUNTIME_INTERNAL
        available_bytes += is_available(block_region) ? block_region->memory.size : 0;
#endif
        if (is_last_block_region(user_context, block_region)) {
            break;
        }
        block_region = block_region->next_ptr;
    }
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "    scanned active regions ("
                        << "block_ptr=" << (void *)block << " "
                        << "total_count=" << (uint32_t)(collected_count + remaining_count) << " "
                        << "block_reserved=" << (uint32_t)(block->reserved) << " "
                        << "scanned_bytes=" << (uint32_t)(scanned_bytes) << " "
                        << "available_bytes=" << (uint32_t)(available_bytes) << " "
                        << ")";
#endif

    if (has_collected) {
#ifdef DEBUG_RUNTIME_INTERNAL
        debug(user_context) << "    collected unused regions ("
                            << "block_ptr=" << (void *)block << " "
                            << "collected_count=" << (uint32_t)collected_count << " "
                            << "remaining_count=" << (uint32_t)remaining_count << " "
                            << "reclaimed=" << (uint32_t)(reserved - block->reserved) << " "
                            << ")";
#endif
    }
    return has_collected;
}

int RegionAllocator::destroy(void *user_context) {
#ifdef DEBUG_RUNTIME_INTERNAL
    debug(user_context) << "RegionAllocator: Destroying all block regions ("
                        << "user_context=" << (void *)(user_context) << ") ...";
#endif
    if (block->regions != nullptr) {
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
    }
    block->reserved = 0;
    block->regions = nullptr;
    block->allocator = nullptr;
    if (arena != nullptr) {
        MemoryArena::destroy(user_context, arena);
    }
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

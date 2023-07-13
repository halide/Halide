#ifndef HALIDE_RUNTIME_MEMORY_RESOURCES_H
#define HALIDE_RUNTIME_MEMORY_RESOURCES_H

#include "../HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// --

// Hint for allocation usage indicating whether or not the resource
// is in use, available, or dedicated (and can't be split or shared)
enum class AllocationStatus {
    InvalidStatus,
    InUse,
    Available,
    Purgeable,
    Dedicated
};

// Hint for allocation requests indicating intended usage
// required between host and device address space mappings
enum class MemoryVisibility {
    InvalidVisibility,  //< invalid enum value
    HostOnly,           //< host local
    DeviceOnly,         //< device local
    DeviceToHost,       //< transfer from device to host
    HostToDevice,       //< transfer from host to device
    DefaultVisibility,  //< default visibility (use any valid visibility -- unable to determine prior to usage)
};

// Hint for allocation requests indicating intended update
// frequency for modifying the contents of the allocation
enum class MemoryUsage {
    InvalidUsage,    //< invalid enum value
    StaticStorage,   //< intended for static storage, whereby the contents will be set once and remain unchanged
    DynamicStorage,  //< intended for dyanmic storage, whereby the contents will be set frequently and change constantly
    UniformStorage,  //< intended for fast & small fixed read-only uniform storage (intended for passing shader parameters), whereby the contents will be set once and remain unchanged
    TransferSrc,     //< intended for staging storage updates, whereby the contents will be used as the source of a transfer
    TransferDst,     //< intended for staging storage updates, whereby the contents will be used as the destination of a transfer
    TransferSrcDst,  //< intended for staging storage updates, whereby the contents will be used either as a source or destination of a transfer
    DefaultUsage     //< default usage (use any valid usage -- unable to determine prior to usage)
};

// Hint for allocation requests indicating ideal caching support (if available)
enum class MemoryCaching {
    InvalidCaching,    //< invalid enum value
    Cached,            //< cached
    Uncached,          //< uncached
    CachedCoherent,    //< cached and coherent
    UncachedCoherent,  //< uncached but still coherent
    DefaultCaching     //< default caching (use any valid caching behaviour -- unable to determine prior to usage)
};

struct MemoryProperties {
    MemoryVisibility visibility = MemoryVisibility::InvalidVisibility;
    MemoryUsage usage = MemoryUsage::InvalidUsage;
    MemoryCaching caching = MemoryCaching::InvalidCaching;
    size_t alignment = 0;         //< required alignment of allocations (zero for no constraint)
    size_t nearest_multiple = 0;  //< require the allocation size to round up to the nearest multiple (zero means no rounding)
};

// Client-facing struct for exchanging memory block allocation requests
struct MemoryBlock {
    void *handle = nullptr;       //< client data storing native handle (managed by alloc_block_region/free_block_region)
    size_t size = 0;              //< allocated size (in bytes)
    bool dedicated = false;       //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    MemoryProperties properties;  //< properties for the allocated block
};

// Client-facing struct for specifying a range of a memory region (eg for crops)
struct MemoryRange {
    size_t head_offset = 0;  //< byte offset from start of region
    size_t tail_offset = 0;  //< byte offset from end of region
};

// Client-facing struct for exchanging memory region allocation requests
struct MemoryRegion {
    void *handle = nullptr;       //< client data storing native handle (managed by alloc_block_region/free_block_region) or a pointer to region owning allocation
    size_t offset = 0;            //< offset from base address in block (in bytes)
    size_t size = 0;              //< allocated size (in bytes)
    MemoryRange range;            //< optional range (e.g. for handling crops, etc)
    bool dedicated = false;       //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    bool is_owner = true;         //< flag indicating whether allocation is owned by this region, in which case handle is a native handle. Otherwise handle points to owning region of alloction.
    MemoryProperties properties;  //< properties for the allocated region
};

// Client-facing struct for issuing memory allocation requests
struct MemoryRequest {
    size_t offset = 0;            //< offset from base address in block (in bytes)
    size_t size = 0;              //< allocated size (in bytes)
    size_t alignment = 0;         //< alignment constraint for address
    bool dedicated = false;       //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    MemoryProperties properties;  //< properties for the allocated region
};

class RegionAllocator;
struct BlockRegion;

// Internal struct for block resource state
// -- Note: first field must MemoryBlock
struct BlockResource {
    MemoryBlock memory;                    //< memory info for the allocated block
    RegionAllocator *allocator = nullptr;  //< designated allocator for the block
    BlockRegion *regions = nullptr;        //< head of linked list of memory regions
    size_t reserved = 0;                   //< number of bytes already reserved to regions
};

// Internal struct for block region state
// -- Note: first field must MemoryRegion
struct BlockRegion {
    MemoryRegion memory;                                        //< memory info for the allocated region
    uint32_t usage_count = 0;                                   //< number of active clients using region
    AllocationStatus status = AllocationStatus::InvalidStatus;  //< allocation status indicator
    BlockRegion *next_ptr = nullptr;                            //< pointer to next block region in linked list
    BlockRegion *prev_ptr = nullptr;                            //< pointer to prev block region in linked list
    BlockResource *block_ptr = nullptr;                         //< pointer to parent block resource
};

// Returns true if given byte alignment is a power of two
ALWAYS_INLINE bool is_power_of_two_alignment(size_t x) {
    return (x & (x - 1)) == 0;
}

// Returns an aligned byte offset to adjust the given offset based on alignment constraints
// -- Alignment must be power of two!
ALWAYS_INLINE size_t aligned_offset(size_t offset, size_t alignment) {
    halide_abort_if_false(nullptr, is_power_of_two_alignment(alignment));
    return (offset + (alignment - 1)) & ~(alignment - 1);
}

// Returns a suitable alignment such that requested alignment is a suitable
// integer multiple of the required alignment
ALWAYS_INLINE size_t conform_alignment(size_t requested, size_t required) {
    size_t alignment = max(requested, required);
    return ((required > 0) && (alignment > required)) ? (required * ((alignment / required) + 1)) : alignment;
}

// Returns a padded size to accommodate an adjusted offset due to alignment constraints
// -- Alignment must be power of two!
ALWAYS_INLINE size_t aligned_size(size_t offset, size_t size, size_t alignment) {
    size_t actual_offset = aligned_offset(offset, alignment);
    size_t padding = actual_offset - offset;
    size_t actual_size = padding + size;
    return actual_size;
}

// Returns a padded size to accommodate an adjusted offset due to alignment constraints rounded up to the nearest multiple
// -- Alignment must be power of two!
ALWAYS_INLINE size_t conform_size(size_t offset, size_t size, size_t alignment, size_t nearest_multiple) {
    size_t adjusted_size = aligned_size(offset, size, alignment);
    adjusted_size = (alignment > adjusted_size) ? alignment : adjusted_size;
    if (nearest_multiple > 0) {
        size_t rounded_size = (((adjusted_size + nearest_multiple - 1) / nearest_multiple) * nearest_multiple);
        return rounded_size;
    } else {
        return adjusted_size;
    }
}

// Clamps the given value to be within the [min_value, max_value] range
ALWAYS_INLINE size_t clamped_size(size_t value, size_t min_value, size_t max_value) {
    size_t result = (value < min_value) ? min_value : value;
    return (result > max_value) ? max_value : result;
}

// Offset the untyped pointer by the given number of bytes
ALWAYS_INLINE const void *offset_address(const void *address, size_t byte_offset) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(address);
    return reinterpret_cast<const void *>(base + byte_offset);
}

// Offset the untyped pointer by the given number of bytes
ALWAYS_INLINE void *offset_address(void *address, size_t byte_offset) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(address);
    return reinterpret_cast<void *>(base + byte_offset);
}

// --

typedef void *(*AllocateSystemFn)(void *, size_t);
typedef void (*DeallocateSystemFn)(void *, void *);

ALWAYS_INLINE void *native_system_malloc(void *user_context, size_t bytes) {
    return malloc(bytes);
}

ALWAYS_INLINE void native_system_free(void *user_context, void *ptr) {
    free(ptr);
}

struct SystemMemoryAllocatorFns {
    AllocateSystemFn allocate = nullptr;
    DeallocateSystemFn deallocate = nullptr;
};

struct HalideSystemAllocatorFns {
    AllocateSystemFn allocate = halide_malloc;
    DeallocateSystemFn deallocate = halide_free;
};

typedef int (*AllocateBlockFn)(void *, MemoryBlock *);
typedef int (*DeallocateBlockFn)(void *, MemoryBlock *);

struct MemoryBlockAllocatorFns {
    AllocateBlockFn allocate = nullptr;
    DeallocateBlockFn deallocate = nullptr;
};

typedef int (*AllocateRegionFn)(void *, MemoryRegion *);
typedef int (*DeallocateRegionFn)(void *, MemoryRegion *);

struct MemoryRegionAllocatorFns {
    AllocateRegionFn allocate = nullptr;
    DeallocateRegionFn deallocate = nullptr;
};

// --

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

// --

extern "C" {

WEAK const char *halide_memory_visibility_name(MemoryVisibility value) {
    switch (value) {
    case MemoryVisibility::InvalidVisibility: {
        return "InvalidVisibility";
    }
    case MemoryVisibility::DefaultVisibility: {
        return "DefaultVisibility";
    }
    case MemoryVisibility::HostOnly: {
        return "HostOnly";
    }
    case MemoryVisibility::DeviceOnly: {
        return "DeviceOnly";
    }
    case MemoryVisibility::HostToDevice: {
        return "HostToDevice";
    }
    case MemoryVisibility::DeviceToHost: {
        return "DeviceToHost";
    }
    default: {
        return "<unknown memory visibility value>";
    }
    };
    return "<unknown memory visibility value>";
}

WEAK const char *halide_memory_usage_name(MemoryUsage value) {
    switch (value) {
    case MemoryUsage::InvalidUsage: {
        return "InvalidUsage";
    }
    case MemoryUsage::DefaultUsage: {
        return "DefaultUsage";
    }
    case MemoryUsage::StaticStorage: {
        return "StaticStorage";
    }
    case MemoryUsage::DynamicStorage: {
        return "DynamicStorage";
    }
    case MemoryUsage::UniformStorage: {
        return "UniformStorage";
    }
    case MemoryUsage::TransferSrc: {
        return "TransferSrc";
    }
    case MemoryUsage::TransferDst: {
        return "TransferDst";
    }
    case MemoryUsage::TransferSrcDst: {
        return "TransferSrcDst";
    }
    default: {
        return "<unknown memory usage value>";
    }
    };
    return "<unknown memory usage value>";
}

WEAK const char *halide_memory_caching_name(MemoryCaching value) {
    switch (value) {
    case MemoryCaching::InvalidCaching: {
        return "InvalidCaching";
    }
    case MemoryCaching::DefaultCaching: {
        return "DefaultCaching";
    }
    case MemoryCaching::Cached: {
        return "Cached";
    }
    case MemoryCaching::Uncached: {
        return "Uncached";
    }
    case MemoryCaching::CachedCoherent: {
        return "CachedCoherent";
    }
    case MemoryCaching::UncachedCoherent: {
        return "UncachedCoherent";
    }
    default: {
        return "<unknown memory visibility value>";
    }
    };
    return "<unknown memory visibility value>";
}

}  // extern "C"

// --

#endif  // HALIDE_RUNTIME_MEMORY_RESOURCES_H

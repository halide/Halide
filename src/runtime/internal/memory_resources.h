#ifndef HALIDE_RUNTIME_MEMORY_RESOURCES_H
#define HALIDE_RUNTIME_MEMORY_RESOURCES_H

namespace Halide {
namespace Runtime {
namespace Internal {

// --

// Hint for allocation usage indicating whether or not the resource
// is in use, available, or dedicated (and can't be split or shared) 
enum AllocationStatus {
    InvalidStatus,
    InUse,
    Available,
    Dedicated
};

// Hint for allocation requests indicating intended usage 
// required between host and device address space mappings 
enum MemoryVisibility {
    InvalidVisibility, //< invalid enum value
    HostOnly,          //< host local
    DeviceOnly,        //< device local
    DeviceToHost,      //< transfer from device to host
    HostToDevice,      //< transfer from host to device
    UnknownVisibility  //< unable to determine prior to usage
};

// Hint for allocation requests indicating intended update 
// frequency for modifying the contents of the allocation 
enum MemoryMutability {
    InvalidMutability,  //< invalid enum value
    Static,             //< intended for static storage, whereby the contents will be set once and remain unchanged
    Dynamic,            //< intended for dyanmic storage, whereby the contents will be set frequently and change constantly
    Transfer,           //< intended for staging storage updates, whereby the contents will be set and used once and then discarded
    UnknownMutability   //< unable to determine prior to usage
};

// Hint for allocation requests indicating ideal caching support (if available) 
enum MemoryCaching {
    InvalidCaching,     //< invalid enum value
    Cached,             //< cached
    Uncached,           //< uncached 
    CachedCoherent,     //< cached and coherent
    UncachedCoherent,   //< uncached but still coherent 
    UnknownCaching      //< unable to determine prior to usage
};

struct MemoryProperties {
    MemoryVisibility visibility = MemoryVisibility::InvalidVisibility;
    MemoryMutability mutability = MemoryMutability::InvalidMutability;
    MemoryCaching caching = MemoryCaching::InvalidCaching;
};

// Client-facing struct for exchanging memory block allocation requests 
struct MemoryBlock {
    void* handle = nullptr;             //< client data storing native handle (managed by alloc_block_region/free_block_region)
    size_t size = 0;                    //< allocated size (in bytes)
    bool dedicated = false;             //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    MemoryProperties properties;        //< properties for the allocated block 
};

// Client-facing struct for exchanging memory region allocation requests 
struct MemoryRegion {
    void* handle = nullptr;             //< client data storing native handle (managed by alloc_block_region/free_block_region)
    size_t offset = 0;                  //< offset from base address in block (in bytes)
    size_t size = 0;                    //< allocated size (in bytes)
    bool dedicated = false;             //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    MemoryProperties properties;        //< properties for the allocated region 
};

// Client-facing struct for issuing memory allocation requests  
struct MemoryRequest {
    size_t offset = 0;                  //< offset from base address in block (in bytes)
    size_t size = 0;                    //< allocated size (in bytes)
    size_t alignment = 0;               //< alignment constraint for address
    bool dedicated = false;             //< flag indicating whether allocation is one dedicated resource (or split/shared into other resources)
    MemoryProperties properties;        //< properties for the allocated region 
};

class RegionAllocator;
struct BlockRegion;

// Internal struct for block resource state 
// -- Note: must header fields must be identical to MemoryBlock 
struct BlockResource {
    MemoryBlock memory = {0};                                  //< memory info for the allocated block
    RegionAllocator* allocator = nullptr;                      //< designated allocator for the block
    BlockRegion* regions = nullptr;                            //< head of linked list of memory regions
    size_t reserved = 0;                                       //< number of bytes already reserved to regions
};

// Internal struct for block region state 
// -- Note: must header fields must be identical to MemoryRegion 
struct BlockRegion {
    MemoryRegion memory = {0};                                 //< memory info for the allocated region
    AllocationStatus status = AllocationStatus::InvalidStatus; //< allocation status indicator
    BlockRegion* next_ptr = nullptr;                           //< pointer to next block region in linked list
    BlockRegion* prev_ptr = nullptr;                           //< pointer to prev block region in linked list
    BlockResource* block_ptr = nullptr;                        //< pointer to parent block resource
};

// Returns an aligned byte offset to adjust the given offset based on alignment constraints
// -- Alignment must be power of two!
inline size_t aligned_offset(size_t offset, size_t alignment) {
    return (offset + (alignment - 1)) & ~(alignment - 1);
}

// Returns a padded size to accomodate an adjusted offset due to alignment constraints
// -- Alignment must be power of two!
inline size_t aligned_size(size_t offset, size_t size, size_t alignment) {
    size_t actual_offset = aligned_offset(offset, alignment);
    size_t padding = actual_offset - offset;
    size_t actual_size = padding + size;
    return actual_size;
}

// Clamps the given value to be within the [min_value, max_value] range
inline size_t clamped_size(size_t value, size_t min_value, size_t max_value) {
    size_t result = (value < min_value) ? min_value : value;
    return (result > max_value) ? max_value : result;
}

// --

class SystemMemoryAllocator {
public:
    SystemMemoryAllocator() = default;
    ~SystemMemoryAllocator() = default;
    
    virtual void* allocate(void* user_context, size_t bytes) = 0;
    virtual void deallocate(void* user_context, void* ptr) = 0;
};

class HalideSystemAllocator : public SystemMemoryAllocator {
public:
    HalideSystemAllocator() = default;
    ~HalideSystemAllocator() = default;

    void* allocate(void* user_context, size_t bytes) override {
        return halide_malloc(user_context, bytes);
    }

    void deallocate(void* user_context, void* ptr) override {
        halide_free(user_context, ptr);
    }
};

class MemoryRegionAllocator {
public:
    MemoryRegionAllocator() = default;
    ~MemoryRegionAllocator() = default;
    
    virtual void allocate(void* user_context, MemoryRegion* region) = 0;
    virtual void deallocate(void* user_context, MemoryRegion* region) = 0;
};

class MemoryBlockAllocator {
public:
    MemoryBlockAllocator() = default;
    ~MemoryBlockAllocator() = default;
    
    virtual void allocate(void* user_context, MemoryBlock* block) = 0;
    virtual void deallocate(void* user_context, MemoryBlock* block) = 0;
};

// --
    
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_MEMORY_RESOURCES_H
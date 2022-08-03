#ifndef HALIDE_RUNTIME_VULKAN_MEMORY_H
#define HALIDE_RUNTIME_VULKAN_MEMORY_H

#include "internal/block_allocator.h"
#include "vulkan_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

// Enable external client to override Vulkan allocation callbacks (if they so desire)
WEAK ScopedSpinLock::AtomicFlag custom_allocation_callbacks_lock = 0;
static const VkAllocationCallbacks *custom_allocation_callbacks = nullptr;  // nullptr => use Vulkan runtime implementation

// --------------------------------------------------------------------------

// Runtime configuration parameters to adjust the behaviour of the block allocator
struct VulkanMemoryConfig {
    size_t minimum_block_size = 32 * 1024 * 1024;  // 32MB
    size_t maximum_block_size = 0;                 //< zero means no contraint
    size_t maximum_block_count = 0;                //< zero means no constraint
};
WEAK VulkanMemoryConfig memory_allocator_config;

// --------------------------------------------------------------------------

/** Vulkan Memory Allocator class interface for managing large 
 * memory requests stored as contiguous blocks of memory, which 
 * are then sub-allocated into smaller regions of 
 * memory to avoid the excessive cost of vkAllocate and the limited
 * number of available allocation calls through the API. 
*/
class VulkanMemoryAllocator {
public:
    // disable copy constructors and assignment
    VulkanMemoryAllocator(const VulkanMemoryAllocator &) = delete;
    VulkanMemoryAllocator &operator=(const VulkanMemoryAllocator &) = delete;

    // disable non-factory constrction
    VulkanMemoryAllocator() = delete;
    ~VulkanMemoryAllocator() = delete;

    // Factory methods for creation / destruction
    static VulkanMemoryAllocator *create(void *user_context, const VulkanMemoryConfig &config,
                                         VkDevice dev, VkPhysicalDevice phys_dev,
                                         const SystemMemoryAllocatorFns &system_allocator,
                                         const VkAllocationCallbacks *alloc_callbacks = nullptr);

    static void destroy(void *user_context, VulkanMemoryAllocator *allocator);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, MemoryRequest &request);
    void reclaim(void *user_context, MemoryRegion *region);
    bool collect(void *user_context);  //< returns true if any blocks were removed
    void release(void *user_context);
    void destroy(void *user_context);

    void *map(void *user_context, MemoryRegion *region);
    void unmap(void *user_context, MemoryRegion *region);

    VkDevice current_device() const {
        return this->device;
    }
    VkPhysicalDevice current_physical_device() const {
        return this->physical_device;
    }
    const VkAllocationCallbacks *callbacks() const {
        return this->alloc_callbacks;
    }

    static const VulkanMemoryConfig &default_config();

    static void allocate_block(void *user_context, MemoryBlock *block);
    static void deallocate_block(void *user_context, MemoryBlock *block);

    static void allocate_region(void *user_context, MemoryRegion *region);
    static void deallocate_region(void *user_context, MemoryRegion *region);

    size_t bytes_allocated_for_blocks() const;
    size_t blocks_allocated() const;

    size_t bytes_allocated_for_regions() const;
    size_t regions_allocated() const;

private:
    static const uint32_t invalid_usage_flags = uint32_t(-1);
    static const uint32_t invalid_memory_type = uint32_t(VK_MAX_MEMORY_TYPES);

    // Initializes a new instance
    void initialize(void *user_context, const VulkanMemoryConfig &config,
                    VkDevice dev, VkPhysicalDevice phys_dev,
                    const SystemMemoryAllocatorFns &system_allocator,
                    const VkAllocationCallbacks *alloc_callbacks = nullptr);

    uint32_t select_memory_usage(void *user_context, MemoryProperties properties) const;

    uint32_t select_memory_type(void *user_context,
                                VkPhysicalDevice physical_device,
                                MemoryProperties properties,
                                uint32_t required_flags) const;


    size_t block_byte_count = 0;
    size_t block_count = 0;
    size_t region_byte_count = 0;
    size_t region_count = 0;
    VulkanMemoryConfig config;
    VkDevice device = nullptr;
    VkPhysicalDevice physical_device = nullptr;
    const VkAllocationCallbacks *alloc_callbacks = nullptr;
    BlockAllocator *block_allocator = nullptr;
};

VulkanMemoryAllocator *VulkanMemoryAllocator::create(void *user_context,
                                                     const VulkanMemoryConfig &cfg, VkDevice dev, VkPhysicalDevice phys_dev,
                                                     const SystemMemoryAllocatorFns &system_allocator,
                                                     const VkAllocationCallbacks *alloc_callbacks) {

    halide_abort_if_false(user_context, system_allocator.allocate != nullptr);
    VulkanMemoryAllocator *result = reinterpret_cast<VulkanMemoryAllocator *>(
        system_allocator.allocate(user_context, sizeof(VulkanMemoryAllocator)));

    if (result == nullptr) {
        error(user_context) << "VulkanMemoryAllocator: Failed to create instance! Out of memory!\n";
        return nullptr;
    }

    result->initialize(user_context, cfg, dev, phys_dev, system_allocator, alloc_callbacks);
    return result;
}

void VulkanMemoryAllocator::destroy(void *user_context, VulkanMemoryAllocator *instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    const BlockAllocator::MemoryAllocators &allocators = instance->block_allocator->current_allocators();
    instance->destroy(user_context);
    BlockAllocator::destroy(user_context, instance->block_allocator);
    halide_abort_if_false(user_context, allocators.system.deallocate != nullptr);
    allocators.system.deallocate(user_context, instance);
}

void VulkanMemoryAllocator::initialize(void *user_context,
                                       const VulkanMemoryConfig &cfg, VkDevice dev, VkPhysicalDevice phys_dev,
                                       const SystemMemoryAllocatorFns &system_allocator,
                                       const VkAllocationCallbacks *callbacks) {

    config = cfg;
    device = dev;
    physical_device = phys_dev;
    alloc_callbacks = callbacks;
    BlockAllocator::MemoryAllocators allocators;
    allocators.system = system_allocator;
    allocators.block = {VulkanMemoryAllocator::allocate_block, VulkanMemoryAllocator::deallocate_block};
    allocators.region = {VulkanMemoryAllocator::allocate_region, VulkanMemoryAllocator::deallocate_region};
    BlockAllocator::Config block_allocator_config = {0};
    block_allocator_config.maximum_block_count = cfg.maximum_block_count;
    block_allocator_config.maximum_block_size = cfg.maximum_block_size;
    block_allocator_config.minimum_block_size = cfg.minimum_block_size;
    block_allocator = BlockAllocator::create(user_context, block_allocator_config, allocators);
    halide_abort_if_false(user_context, block_allocator != nullptr);
}

MemoryRegion *VulkanMemoryAllocator::reserve(void *user_context, MemoryRequest &request) {
    debug(nullptr) << "VulkanMemoryAllocator: Reserving memory ("
             << "user_context=" << user_context << " "
             << "block_allocator=" << (void *)(block_allocator) << " "
             << "request_size=" << (uint32_t)(request.size) << " "
             << "device=" << (void *)(device) << " "
             << "physical_device=" << (void *)(physical_device) << ") ...\n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, block_allocator != nullptr);
    return block_allocator->reserve(this, request);
}

void *VulkanMemoryAllocator::map(void *user_context, MemoryRegion *region) {
    debug(nullptr) << "VulkanMemoryAllocator: Mapping region ("
             << "user_context=" << user_context << " "
             << "region=" << (void *)(region) << " "
             << "device=" << (void *)(device) << " "
             << "physical_device=" << (void *)(physical_device) << ") ...\n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, block_allocator != nullptr);

    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    if (region_allocator == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to map region! Invalid region allocator handle!\n";
        return nullptr;
    }

    BlockResource *block_resource = region_allocator->block_resource();
    if (block_resource == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to map region! Invalid block resource handle!\n";
        return nullptr;
    }

    VkDeviceMemory *device_memory = reinterpret_cast<VkDeviceMemory *>(block_resource->memory.handle);
    if (device_memory == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to map region! Invalid device memory handle!\n";
        return nullptr;
    }

    uint8_t *mapped_ptr = nullptr;
    VkResult result = vkMapMemory(device, *device_memory, region->offset, region->size, 0, (void **)(&mapped_ptr));
    if (result != VK_SUCCESS) {
        error(user_context) << "VulkanMemoryAllocator: Mapping region failed! vkMapMemory returned error code: " << vk_get_error_name(result) << "\n";
        return nullptr;
    }

    return mapped_ptr;
}

void VulkanMemoryAllocator::unmap(void *user_context, MemoryRegion *region) {
    debug(nullptr) << "VulkanMemoryAllocator: Unmapping region ("
             << "user_context=" << user_context << " "
             << "region=" << (void *)(region) << " "
             << "device=" << (void *)(device) << " "
             << "physical_device=" << (void *)(physical_device) << ") ...\n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);

    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    if (region_allocator == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to unmap region! Invalid region allocator handle!\n";
        return;
    }

    BlockResource *block_resource = region_allocator->block_resource();
    if (block_resource == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to unmap region! Invalid block resource handle!\n";
        return;
    }

    VkDeviceMemory *device_memory = reinterpret_cast<VkDeviceMemory *>(block_resource->memory.handle);
    if (device_memory == nullptr) {
        error(nullptr) << "VulkanMemoryAllocator: Unable to unmap region! Invalid device memory handle!\n";
        return;
    }

    vkUnmapMemory(device, *device_memory);
}

void VulkanMemoryAllocator::reclaim(void *user_context, MemoryRegion *region) {
    debug(nullptr) << "VulkanMemoryAllocator: Reclaiming region ("
             << "user_context=" << user_context << " "
             << "region=" << (void *)(region) << ") ... \n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);

    return block_allocator->reclaim(this, region);
}

bool VulkanMemoryAllocator::collect(void *user_context) {
    debug(nullptr) << "VulkanMemoryAllocator: Collecting unused memory ("
             << "user_context=" << user_context << ") ... \n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);

    return block_allocator->collect(this);
}

void VulkanMemoryAllocator::release(void *user_context) {
    debug(nullptr) << "VulkanMemoryAllocator: Releasing ("
             << "user_context=" << user_context << ") ... \n";

    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);

    block_allocator->release(this);
}

void VulkanMemoryAllocator::destroy(void *user_context) {
    debug(nullptr) << "VulkanMemoryAllocator: Destroying allocator ("
             << "user_context=" << user_context << ") ... \n";

    block_allocator->destroy(this);
}

const VulkanMemoryConfig &
VulkanMemoryAllocator::default_config() {
    static VulkanMemoryConfig result;
    return result;
}

// --

void VulkanMemoryAllocator::allocate_block(void *user_context, MemoryBlock *block) {
    debug(nullptr) << "VulkanMemoryAllocator: Allocating block ("
             << "user_context=" << user_context << " "
             << "block=" << (void *)(block) << ") ... \n";

    VulkanMemoryAllocator *instance = reinterpret_cast<VulkanMemoryAllocator *>(user_context);
    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, instance->device != nullptr);
    halide_abort_if_false(user_context, instance->physical_device != nullptr);
    halide_abort_if_false(user_context, block != nullptr);

    debug(nullptr) << "VulkanMemoryAllocator: Allocating block ("
             << "size=" << (uint32_t)block->size << ", "
             << "dedicated=" << (block->dedicated ? "true" : "false") << " "
             << "usage=" << halide_memory_usage_name(block->properties.usage) << " "
             << "caching=" << halide_memory_caching_name(block->properties.caching) << " "
             << "visibility=" << halide_memory_visibility_name(block->properties.visibility) << ")\n";

    // Find an appropriate memory type given the flags
    uint32_t memory_type = instance->select_memory_type(user_context, instance->physical_device, block->properties, 0);
    if (memory_type == invalid_memory_type) {
        debug(nullptr) << "VulkanMemoryAllocator: Unable to find appropriate memory type for device!\n";
        return;
    }

    // Allocate memory
    VkMemoryAllocateInfo alloc_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,  // struct type
        nullptr,                                 // struct extending this
        block->size,                             // size of allocation in bytes
        memory_type                              // memory type index from physical device
    };

    VkDeviceMemory *device_memory = (VkDeviceMemory *)vk_host_malloc(nullptr, sizeof(VkDeviceMemory), 0, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, instance->alloc_callbacks);
    if (device_memory == nullptr) {
        error(nullptr) << "VulkanBlockAllocator: Unable to allocate block! Failed to allocate device memory handle!\n";
        return;
    }

    VkResult result = vkAllocateMemory(instance->device, &alloc_info, instance->alloc_callbacks, device_memory);
    if (result != VK_SUCCESS) {
        error(nullptr) << "VulkanMemoryAllocator: Allocation failed! vkAllocateMemory returned: " << vk_get_error_name(result) << "\n";
        return;
    }

    block->handle = (void *)device_memory;
    instance->block_byte_count += block->size;
    instance->block_count++;
}

void VulkanMemoryAllocator::deallocate_block(void *user_context, MemoryBlock *block) {
    debug(nullptr) << "VulkanMemoryAllocator: Deallocating block ("
             << "user_context=" << user_context << " "
             << "block=" << (void *)(block) << ") ... \n";

    VulkanMemoryAllocator *instance = reinterpret_cast<VulkanMemoryAllocator *>(user_context);
    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, instance->device != nullptr);
    halide_abort_if_false(user_context, instance->physical_device != nullptr);
    halide_abort_if_false(user_context, block != nullptr);

    debug(nullptr) << "VulkanBlockAllocator: deallocating block ("
             << "size=" << (uint32_t)block->size << ", "
             << "dedicated=" << (block->dedicated ? "true" : "false") << " "
             << "usage=" << halide_memory_usage_name(block->properties.usage) << " "
             << "caching=" << halide_memory_caching_name(block->properties.caching) << " "
             << "visibility=" << halide_memory_visibility_name(block->properties.visibility) << ")\n";

    if (block->handle == nullptr) {
        debug(nullptr) << "VulkanBlockAllocator: Unable to deallocate block! Invalid handle!\n";
        return;
    }

    VkDeviceMemory *device_memory = reinterpret_cast<VkDeviceMemory *>(block->handle);
    if (device_memory == nullptr) {
        debug(nullptr) << "VulkanBlockAllocator: Unable to deallocate block! Invalid device memory handle!\n";
        return;
    }

    vkFreeMemory(instance->device, *device_memory, instance->alloc_callbacks);
    instance->block_byte_count -= block->size;
    instance->block_count--;

    vk_host_free(nullptr, device_memory, instance->alloc_callbacks);
    device_memory = nullptr;
}

size_t VulkanMemoryAllocator::blocks_allocated() const {
    return block_count;
}

size_t VulkanMemoryAllocator::bytes_allocated_for_blocks() const {
    return block_byte_count;
}

uint32_t VulkanMemoryAllocator::select_memory_type(void *user_context,
                                                   VkPhysicalDevice physical_device,
                                                   MemoryProperties properties,
                                                   uint32_t required_flags) const {

    uint32_t want_flags = 0;  //< preferred memory flags for requested access type
    uint32_t need_flags = 0;  //< must have in order to enable requested access
    switch (properties.visibility) {
    case MemoryVisibility::HostOnly:
        want_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        break;
    case MemoryVisibility::DeviceOnly:
        need_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryVisibility::DeviceToHost:
        need_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        want_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
    case MemoryVisibility::HostToDevice:
        need_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        break;
    case MemoryVisibility::DefaultVisibility:
    case MemoryVisibility::InvalidVisibility:
    default:
        debug(nullptr) << "VulkanMemoryAllocator: Unable to convert type! Invalid memory visibility request!\n\t"
                 << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_memory_type;
    };

    switch (properties.caching) {
    case MemoryCaching::CachedCoherent:
        if (need_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            want_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        break;
    case MemoryCaching::UncachedCoherent:
        if (need_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            want_flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        break;
    case MemoryCaching::Cached:
        if (need_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            want_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        }
        break;
    case MemoryCaching::Uncached:
    case MemoryCaching::DefaultCaching:
        break;
    case MemoryCaching::InvalidCaching:
    default:
        debug(nullptr) << "VulkanMemoryAllocator: Unable to convert type! Invalid memory caching request!\n\t"
                 << "caching=" << halide_memory_caching_name(properties.caching) << "\n";
        return invalid_memory_type;
    };

    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);

    uint32_t result = invalid_memory_type;
    for (uint32_t i = 0; i < device_memory_properties.memoryTypeCount; ++i) {

        // if required flags are given, see if the memory type matches the requirement
        if (required_flags) {
            if (((required_flags >> i) & 1) == 0) {
                continue;
            }
        }

        const VkMemoryPropertyFlags properties = device_memory_properties.memoryTypes[i].propertyFlags;
        if (need_flags) {
            if ((properties & need_flags) != need_flags) {
                continue;
            }
        }

        if (want_flags) {
            if ((properties & want_flags) != want_flags) {
                continue;
            }
        }

        result = i;
        break;
    }

    if (result == invalid_memory_type) {
        debug(nullptr) << "VulkanBlockAllocator: Failed to find appropriate memory type for given properties:\n\t"
                 << "usage=" << halide_memory_usage_name(properties.usage) << " "
                 << "caching=" << halide_memory_caching_name(properties.caching) << " "
                 << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_memory_type;
    }

    return result;
}

// --

void VulkanMemoryAllocator::allocate_region(void *user_context, MemoryRegion *region) {
    debug(nullptr) << "VulkanMemoryAllocator: Allocating region ("
             << "user_context=" << user_context << " "
             << "region=" << (void *)(region) << ") ... \n";

    VulkanMemoryAllocator *instance = reinterpret_cast<VulkanMemoryAllocator *>(user_context);
    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, instance->device != nullptr);
    halide_abort_if_false(user_context, instance->physical_device != nullptr);
    halide_abort_if_false(user_context, region != nullptr);

    debug(nullptr) << "VulkanRegionAllocator: Allocating region ("
             << "size=" << (uint32_t)region->size << ", "
             << "offset=" << (uint32_t)region->offset << ", "
             << "dedicated=" << (region->dedicated ? "true" : "false") << " "
             << "usage=" << halide_memory_usage_name(region->properties.usage) << " "
             << "caching=" << halide_memory_caching_name(region->properties.caching) << " "
             << "visibility=" << halide_memory_visibility_name(region->properties.visibility) << ")\n";

    uint32_t usage_flags = instance->select_memory_usage(user_context, region->properties);

    VkBufferCreateInfo create_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,  // struct type
        nullptr,                               // struct extending this
        0,                                     // create flags
        region->size,                          // buffer size (in bytes)
        usage_flags,                           // buffer usage flags
        VK_SHARING_MODE_EXCLUSIVE,             // sharing mode
        0, nullptr};

    VkBuffer *buffer = (VkBuffer *)vk_host_malloc(nullptr, sizeof(VkBuffer), 0, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, instance->alloc_callbacks);
    if (buffer == nullptr) {
        error(nullptr) << "VulkanRegionAllocator: Unable to allocate region! Failed to allocate buffer handle!\n";
        return;
    }

    VkResult result = vkCreateBuffer(instance->device, &create_info, instance->alloc_callbacks, buffer);
    if (result != VK_SUCCESS) {
        error(nullptr) << "VulkanRegionAllocator: Failed to create buffer!\n\t"
                 << "vkCreateBuffer returned: " << vk_get_error_name(result) << "\n";
        return;
    }

    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    BlockResource *block_resource = region_allocator->block_resource();
    if (block_resource == nullptr) {
        error(nullptr) << "VulkanBlockAllocator: Unable to allocate region! Invalid block resource handle!\n";
        return;
    }

    VkDeviceMemory *device_memory = reinterpret_cast<VkDeviceMemory *>(block_resource->memory.handle);
    if (device_memory == nullptr) {
        error(nullptr) << "VulkanBlockAllocator: Unable to allocate region! Invalid device memory handle!\n";
        return;
    }

    // Finally, bind buffer to the device memory
    result = vkBindBufferMemory(instance->device, *buffer, *device_memory, region->offset);
    if (result != VK_SUCCESS) {
        error(nullptr) << "VulkanRegionAllocator: Failed to bind buffer!\n\t"
                 << "vkBindBufferMemory returned: " << vk_get_error_name(result) << "\n";
        return;
    }

    region->handle = (void *)buffer;
    instance->region_byte_count += region->size;
    instance->region_count++;
}

void VulkanMemoryAllocator::deallocate_region(void *user_context, MemoryRegion *region) {
    debug(nullptr) << "VulkanMemoryAllocator: Deallocating region ("
             << "user_context=" << user_context << " "
             << "region=" << (void *)(region) << ") ... \n";

    VulkanMemoryAllocator *instance = reinterpret_cast<VulkanMemoryAllocator *>(user_context);
    halide_abort_if_false(user_context, instance != nullptr);
    halide_abort_if_false(user_context, instance->device != nullptr);
    halide_abort_if_false(user_context, instance->physical_device != nullptr);
    halide_abort_if_false(user_context, region != nullptr);
    debug(nullptr) << "VulkanRegionAllocator: Deallocating region ("
             << "size=" << (uint32_t)region->size << ", "
             << "offset=" << (uint32_t)region->offset << ", "
             << "dedicated=" << (region->dedicated ? "true" : "false") << " "
             << "usage=" << halide_memory_usage_name(region->properties.usage) << " "
             << "caching=" << halide_memory_caching_name(region->properties.caching) << " "
             << "visibility=" << halide_memory_visibility_name(region->properties.visibility) << ")\n";

    if (region->handle == nullptr) {
        debug(nullptr) << "VulkanRegionAllocator: Unable to deallocate region! Invalid handle!\n";
        return;
    }

    VkBuffer *buffer = reinterpret_cast<VkBuffer *>(region->handle);
    if (buffer == nullptr) {
        error(nullptr) << "VulkanRegionAllocator: Unable to deallocate region! Invalid buffer handle!\n";
        return;
    }

    vkDestroyBuffer(instance->device, *buffer, instance->alloc_callbacks);
    region->handle = nullptr;
    instance->region_byte_count -= region->size;
    instance->region_count--;

    vk_host_free(nullptr, buffer, instance->alloc_callbacks);
    buffer = nullptr;
}

size_t VulkanMemoryAllocator::regions_allocated() const {
    return region_count;
}

size_t VulkanMemoryAllocator::bytes_allocated_for_regions() const {
    return region_byte_count;
}

uint32_t VulkanMemoryAllocator::select_memory_usage(void *user_context, MemoryProperties properties) const {
    uint32_t result = 0;
    switch (properties.usage) {
    case MemoryUsage::UniformStorage:
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        break;
    case MemoryUsage::DynamicStorage:
    case MemoryUsage::StaticStorage:
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        break;
    case MemoryUsage::TransferSrc:
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        break;
    case MemoryUsage::TransferDst:
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case MemoryUsage::TransferSrcDst:
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        break;
    case MemoryUsage::DefaultUsage:
    case MemoryUsage::InvalidUsage:
    default:
        error(user_context) << "VulkanRegionAllocator: Unable to convert type! Invalid memory usage request!\n\t"
                            << "usage=" << halide_memory_usage_name(properties.usage) << "\n";
        return invalid_usage_flags;
    };

    if (result == invalid_usage_flags) {
        error(user_context) << "VulkanRegionAllocator: Failed to find appropriate memory usage for given properties:\n\t"
                            << "usage=" << halide_memory_usage_name(properties.usage) << " "
                            << "caching=" << halide_memory_caching_name(properties.caching) << " "
                            << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_usage_flags;
    }

    return result;
}

// --------------------------------------------------------------------------

namespace {

// --------------------------------------------------------------------------
// Halide System allocator for host allocations
void *vk_system_malloc(void *user_context, size_t size) {
    return malloc(size);
}

void vk_system_free(void *user_context, void *ptr) {
    free(ptr);
}

// Vulkan host-side allocation
void *vk_host_malloc(void *user_context, size_t size, size_t alignment, VkSystemAllocationScope scope, const VkAllocationCallbacks *callbacks) {
    if (callbacks) {
        return callbacks->pfnAllocation(user_context, size, alignment, scope);
    } else {
        return vk_system_malloc(user_context, size);
    }
}

void vk_host_free(void *user_context, void *ptr, const VkAllocationCallbacks *callbacks) {
    if (callbacks) {
        return callbacks->pfnFree(user_context, ptr);
    } else {
        return vk_system_free(user_context, ptr);
    }
}

VulkanMemoryAllocator *vk_create_memory_allocator(void *user_context,
                                                       VkDevice device,
                                                       VkPhysicalDevice physical_device,
                                                       const VkAllocationCallbacks *alloc_callbacks) {

    SystemMemoryAllocatorFns system_allocator = {vk_system_malloc, vk_system_free};

    return VulkanMemoryAllocator::create(user_context,
                                         memory_allocator_config, device, physical_device,
                                         system_allocator, alloc_callbacks);
}

int vk_destroy_memory_allocator(void *user_context, VulkanMemoryAllocator *allocator) {
    if (allocator != nullptr) {
        VulkanMemoryAllocator::destroy(user_context, allocator);
        allocator = nullptr;
    }
    return halide_error_code_success;
}

// --------------------------------------------------------------------------

}  // namespace: (anonymous)
}  // namespace: Vulkan
}  // namespace: Internal
}  // namespace: Runtime
}  // namespace: Halide

// --------------------------------------------------------------------------

extern "C" {

// --------------------------------------------------------------------------

WEAK void halide_vulkan_set_allocation_callbacks(const VkAllocationCallbacks *callbacks) {
    using namespace Halide::Runtime::Internal::Vulkan;
    ScopedSpinLock lock(&custom_allocation_callbacks_lock);
    custom_allocation_callbacks = callbacks;
}

WEAK const VkAllocationCallbacks *halide_vulkan_get_allocation_callbacks(void *user_context) {
    using namespace Halide::Runtime::Internal::Vulkan;
    ScopedSpinLock lock(&custom_allocation_callbacks_lock);
    return custom_allocation_callbacks;
}

// --------------------------------------------------------------------------

}  // extern "C"

#endif  // HALIDE_RUNTIME_VULKAN_MEMORY_H
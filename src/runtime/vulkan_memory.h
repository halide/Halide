#ifndef HALIDE_RUNTIME_VULKAN_MEMORY_H
#define HALIDE_RUNTIME_VULKAN_MEMORY_H

#include "internal/block_allocator.h"
#include "vulkan_context.h"
#include "vulkan_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --

// Halide System allocator for host allocations
WEAK HalideSystemAllocator system_allocator;

// Vulkan Memory allocator for host-device allocations
class VulkanMemoryAllocator;
WEAK VulkanMemoryAllocator *memory_allocator = nullptr;

// Runtime configuration parameters to adjust the behaviour of the block allocator
struct VulkanMemoryConfig {
    size_t minimum_block_size = 128 * 1024 * 1024;  // 128MB
    size_t maximum_block_size = 0;                  //< zero means no contraint
    size_t maximum_block_count = 0;                 //< zero means no constraint
};
WEAK VulkanMemoryConfig memory_allocator_config;

// --

/** Vulkan Block Allocator class interface for allocating large
 * blocks of device memory from specific heaps based on memory properties 
 */
class VulkanBlockAllocator : public MemoryBlockAllocator {
public:
    VulkanBlockAllocator() = default;
    ~VulkanBlockAllocator() = default;

    void allocate(void *user_context, MemoryBlock *block) override;
    void deallocate(void *user_context, MemoryBlock *block) override;

    void bind(void *user_context, VkDevice device, VkPhysicalDevice physical_device);
    void unbind(void *user_context);

    size_t bytes_allocated() const;
    size_t blocks_allocated() const;

private:
    static const uint32_t invalid_flags = uint32_t(VK_MAX_MEMORY_TYPES);

    uint32_t select_memory_type(void *user_context,
                                VkPhysicalDevice physical_device,
                                MemoryProperties properties,
                                uint32_t required_flags) const;

private:
    size_t byte_count = 0;
    size_t block_count = 0;
    VkDevice device = nullptr;
    VkPhysicalDevice physical_device = nullptr;
};

// --

void VulkanBlockAllocator::allocate(void *user_context, MemoryBlock *block) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, block != nullptr);

    debug(user_context) << "VulkanBlockAllocator: allocating block ("
                        << "size=" << (uint32_t)block->size << ", "
                        << "dedicated=" << (block->dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(block->properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(block->properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(block->properties.visibility) << ")\n";

    // Find an appropriate memory type given the flags
    uint32_t memory_type = select_memory_type(user_context, physical_device, block->properties, 0);
    if (memory_type == invalid_flags) {
        debug(user_context) << "VulkanBlockAllocator: Unable to find appropriate memory type for device!\n";
        return;
    }

    // Allocate memory
    VkMemoryAllocateInfo alloc_info = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,  // struct type
        nullptr,                                 // struct extending this
        block->size,                             // size of allocation in bytes
        memory_type                              // memory type index from physical device
    };

    VkDeviceMemory device_memory = {0};
    VkResult result = vkAllocateMemory(device, &alloc_info, nullptr, &device_memory);
    if (result != VK_SUCCESS) {
        debug(user_context) << "VulkanBlockAllocator: Allocation failed! vkAllocateMemory returned: " << get_vulkan_error_name(result) << "\n";
        return;
    }

    block->handle = (void *)device_memory;
    byte_count += block->size;
    block_count++;
}

void VulkanBlockAllocator::deallocate(void *user_context, MemoryBlock *block) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, block != nullptr);
    debug(user_context) << "VulkanBlockAllocator: deallocating block ("
                        << "size=" << (uint32_t)block->size << ", "
                        << "dedicated=" << (block->dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(block->properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(block->properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(block->properties.visibility) << ")\n";

    if (block->handle == nullptr) {
        debug(user_context) << "VulkanBlockAllocator: Unable to deallocate block! Invalid handle!\n";
        return;
    }

    VkDeviceMemory device_memory = reinterpret_cast<VkDeviceMemory>(block->handle);
    vkFreeMemory(device, device_memory, nullptr);
    byte_count -= block->size;
    block_count--;
}

void VulkanBlockAllocator::bind(void *context, VkDevice dev, VkPhysicalDevice physical_dev) {
    device = dev;
    physical_device = physical_dev;
}

void VulkanBlockAllocator::unbind(void *context) {
    device = nullptr;
    physical_device = nullptr;
}

size_t VulkanBlockAllocator::blocks_allocated() const {
    return block_count;
}

size_t VulkanBlockAllocator::bytes_allocated() const {
    return byte_count;
}

uint32_t VulkanBlockAllocator::select_memory_type(void *user_context,
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
        error(user_context) << "VulkanBlockAllocator: Unable to convert type! Invalid memory visibility request!\n\t"
                            << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_flags;
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
        error(user_context) << "VulkanBlockAllocator: Unable to convert type! Invalid memory caching request!\n\t"
                            << "caching=" << halide_memory_caching_name(properties.caching) << "\n";
        return invalid_flags;
    };

    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);

    uint32_t result = invalid_flags;
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

    if (result == invalid_flags) {
        error(user_context) << "VulkanBlockAllocator: Failed to find appropriate memory type for given properties:\n\t"
                            << "usage=" << halide_memory_usage_name(properties.usage) << " "
                            << "caching=" << halide_memory_caching_name(properties.caching) << " "
                            << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_flags;
    }

    return result;
}

// --

/** Vulkan Region Allocator class interface for sub-allocating existing 
 * memory allocations into Vulkan Buffers with specific offsets 
 */
class VulkanRegionAllocator : public MemoryRegionAllocator {
public:
    VulkanRegionAllocator() = default;
    ~VulkanRegionAllocator() = default;

    void allocate(void *user_context, MemoryRegion *region) override;
    void deallocate(void *user_context, MemoryRegion *region) override;

    void bind(void *user_context, VkDevice device, VkPhysicalDevice physical_device);
    void unbind(void *user_context);

    size_t bytes_allocated() const;
    size_t regions_allocated() const;

private:
    static const uint32_t invalid_flags = uint32_t(-1);
    uint32_t select_memory_usage(void *user_context, MemoryProperties properties) const;

private:
    size_t byte_count = 0;
    size_t region_count = 0;
    VkDevice device = nullptr;
    VkPhysicalDevice physical_device = nullptr;
};

// --

void VulkanRegionAllocator::allocate(void *user_context, MemoryRegion *region) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, region != nullptr);
    debug(user_context) << "VulkanRegionAllocator: allocating region ("
                        << "size=" << (uint32_t)region->size << ", "
                        << "offset=" << (uint32_t)region->offset << ", "
                        << "dedicated=" << (region->dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(region->properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(region->properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(region->properties.visibility) << ")\n";

    uint32_t usage_flags = select_memory_usage(user_context, region->properties);

    VkBufferCreateInfo create_info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,  // struct type
        nullptr,                               // struct extending this
        0,                                     // create flags
        region->size,                          // buffer size (in bytes)
        usage_flags,                           // buffer usage flags
        VK_SHARING_MODE_EXCLUSIVE,             // sharing mode
        0, nullptr};

    VkBuffer buffer = {0};
    VkResult result = vkCreateBuffer(device, &create_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        error(user_context) << "VulkanRegionAllocator: Failed to create buffer!\n\t"
                            << "vkCreateBuffer returned: " << get_vulkan_error_name(result) << "\n";
        return;
    }

    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    BlockResource *block_resource = region_allocator->block_resource();
    VkDeviceMemory device_memory = reinterpret_cast<VkDeviceMemory>(block_resource->memory.handle);

    // Finally, bind buffer to the device memory
    result = vkBindBufferMemory(device, buffer, device_memory, region->offset);
    if (result != VK_SUCCESS) {
        error(user_context) << "VulkanRegionAllocator: Failed to bind buffer!\n\t"
                            << "vkBindBufferMemory returned: " << get_vulkan_error_name(result) << "\n";
        return;
    }

    region->handle = (void *)buffer;
    byte_count += region->size;
    region_count++;
}

void VulkanRegionAllocator::deallocate(void *user_context, MemoryRegion *region) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    halide_abort_if_false(user_context, region != nullptr);
    debug(user_context) << "VulkanRegionAllocator: deallocating region ("
                        << "size=" << (uint32_t)region->size << ", "
                        << "offset=" << (uint32_t)region->offset << ", "
                        << "dedicated=" << (region->dedicated ? "true" : "false") << " "
                        << "usage=" << halide_memory_usage_name(region->properties.usage) << " "
                        << "caching=" << halide_memory_caching_name(region->properties.caching) << " "
                        << "visibility=" << halide_memory_visibility_name(region->properties.visibility) << ")\n";

    if (region->handle == nullptr) {
        debug(user_context) << "VulkanRegionAllocator: Unable to deallocate region! Invalid handle!\n";
        return;
    }

    VkBuffer buffer = (VkBuffer)region->handle;
    vkDestroyBuffer(device, buffer, nullptr);

    region->handle = nullptr;
    byte_count -= region->size;
    region_count--;
}

void VulkanRegionAllocator::bind(void *user_context, VkDevice dev, VkPhysicalDevice physical_dev) {
    device = dev;
    physical_device = physical_dev;
}

void VulkanRegionAllocator::unbind(void *user_context) {
    device = nullptr;
    physical_device = nullptr;
}

size_t VulkanRegionAllocator::regions_allocated() const {
    return region_count;
}

size_t VulkanRegionAllocator::bytes_allocated() const {
    return byte_count;
}

uint32_t VulkanRegionAllocator::select_memory_usage(void *user_context, MemoryProperties properties) const {
    uint32_t result = 0;
    switch (properties.usage) {
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
        return invalid_flags;
    };

    if (result == invalid_flags) {
        error(user_context) << "VulkanRegionAllocator: Failed to find appropriate memory usage for given properties:\n\t"
                            << "usage=" << halide_memory_usage_name(properties.usage) << " "
                            << "caching=" << halide_memory_caching_name(properties.caching) << " "
                            << "visibility=" << halide_memory_visibility_name(properties.visibility) << "\n";
        return invalid_flags;
    }

    return result;
}

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
    static VulkanMemoryAllocator *create(void *user_context, const VulkanMemoryConfig &config, SystemMemoryAllocator *system_allocator);
    static void destroy(void *user_context, VulkanMemoryAllocator *allocator);

    // Public interface methods
    MemoryRegion *reserve(void *user_context, MemoryRequest &request);
    void reclaim(void *user_context, MemoryRegion *region);
    bool collect(void *user_context);  //< returns true if any blocks were removed
    void destroy(void *user_context);

    void *map(void *user_context, MemoryRegion *region);
    void unmap(void *user_context, MemoryRegion *region);

    void bind(void *user_context, VkDevice device, VkPhysicalDevice physical_device);
    void unbind(void *user_context);

    static const VulkanMemoryConfig &default_config();

private:
    // Initializes a new instance
    void initialize(void *user_context, const VulkanMemoryConfig &config, SystemMemoryAllocator *system_allocator);

    VulkanMemoryConfig config;
    VkDevice device = nullptr;
    VkPhysicalDevice physical_device = nullptr;
    VulkanBlockAllocator memory_block_allocator;
    VulkanRegionAllocator memory_region_allocator;
    BlockAllocator *block_allocator = nullptr;
    ScopedSpinLock::AtomicFlag spin_lock = 0;
};

VulkanMemoryAllocator *VulkanMemoryAllocator::create(void *user_context, const VulkanMemoryConfig &cfg, SystemMemoryAllocator *system_allocator) {
    halide_abort_if_false(user_context, system_allocator != nullptr);
    VulkanMemoryAllocator *result = reinterpret_cast<VulkanMemoryAllocator *>(
        system_allocator->allocate(user_context, sizeof(VulkanMemoryAllocator)));

    if (result == nullptr) {
        error(user_context) << "VulkanMemoryAllocator: Failed to create instance! Out of memory!\n";
        return nullptr;
    }

    result->initialize(user_context, cfg, system_allocator);
    return result;
}

void VulkanMemoryAllocator::destroy(void *user_context, VulkanMemoryAllocator *instance) {
    halide_abort_if_false(user_context, instance != nullptr);
    const BlockAllocator::MemoryAllocators &allocators = instance->block_allocator->current_allocators();
    instance->destroy(user_context);
    BlockAllocator::destroy(user_context, instance->block_allocator);
    halide_abort_if_false(user_context, allocators.system != nullptr);
    allocators.system->deallocate(user_context, instance);
}

void VulkanMemoryAllocator::initialize(void *user_context, const VulkanMemoryConfig &cfg, SystemMemoryAllocator *system_allocator) {
    config = cfg;
    device = nullptr;
    physical_device = nullptr;
    BlockAllocator::MemoryAllocators allocators = {0};
    allocators.system = system_allocator;
    allocators.block = &memory_block_allocator;
    allocators.region = &memory_region_allocator;
    BlockAllocator::Config block_allocator_config = {0};
    block_allocator_config.maximum_block_count = cfg.maximum_block_count;
    block_allocator_config.maximum_block_size = cfg.maximum_block_size;
    block_allocator_config.minimum_block_size = cfg.minimum_block_size;
    block_allocator = BlockAllocator::create(user_context, block_allocator_config, allocators);
    spin_lock = 0;
}

MemoryRegion *VulkanMemoryAllocator::reserve(void *user_context, MemoryRequest &request) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    return block_allocator->reserve(user_context, request);
}

void *VulkanMemoryAllocator::map(void *user_context, MemoryRegion *region) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    BlockResource *block_resource = region_allocator->block_resource();
    VkDeviceMemory device_memory = reinterpret_cast<VkDeviceMemory>(block_resource->memory.handle);

    uint8_t *mapped_ptr = nullptr;
    VkResult result = vkMapMemory(device, device_memory, region->offset, region->size, 0, (void **)(&mapped_ptr));
    if (result != VK_SUCCESS) {
        error(user_context) << "VulkanMemoryAllocator: Mapping region failed! vkMapMemory returned error code: " << get_vulkan_error_name(result) << "\n";
        return nullptr;
    }

    return mapped_ptr;
}

void VulkanMemoryAllocator::unmap(void *user_context, MemoryRegion *region) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    RegionAllocator *region_allocator = RegionAllocator::find_allocator(user_context, region);
    BlockResource *block_resource = region_allocator->block_resource();
    VkDeviceMemory device_memory = reinterpret_cast<VkDeviceMemory>(block_resource->memory.handle);
    vkUnmapMemory(device, device_memory);
}

WEAK void VulkanMemoryAllocator::bind(void *user_context, VkDevice dev, VkPhysicalDevice physical_dev) {
    device = dev;
    physical_device = physical_dev;
    memory_block_allocator.bind(user_context, device, physical_device);
    memory_region_allocator.bind(user_context, device, physical_device);
}

WEAK void VulkanMemoryAllocator::unbind(void *user_context) {
    device = nullptr;
    physical_device = nullptr;
    memory_block_allocator.unbind(user_context);
    memory_region_allocator.unbind(user_context);
}

void VulkanMemoryAllocator::reclaim(void *user_context, MemoryRegion *region) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    return block_allocator->reclaim(user_context, region);
}

bool VulkanMemoryAllocator::collect(void *user_context) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    return block_allocator->collect(user_context);
}

void VulkanMemoryAllocator::destroy(void *user_context) {
    halide_abort_if_false(user_context, device != nullptr);
    halide_abort_if_false(user_context, physical_device != nullptr);
    ScopedSpinLock lock(&spin_lock);
    block_allocator->destroy(user_context);
}

const VulkanMemoryConfig &
VulkanMemoryAllocator::default_config() {
    static VulkanMemoryConfig result;
    return result;
}

// --

WEAK int vk_create_memory_allocator(void *user_context) {
    if (memory_allocator != nullptr) {
        return halide_error_code_success;
    }

    memory_allocator = VulkanMemoryAllocator::create(user_context, memory_allocator_config, &system_allocator);
    if (memory_allocator == nullptr) {
        return halide_error_code_out_of_memory;
    }
    return halide_error_code_success;
}

WEAK int vk_destroy_memory_allocator(void *user_context) {
    if (memory_allocator == nullptr) {
        return halide_error_code_success;
    }

    VulkanMemoryAllocator::destroy(user_context, memory_allocator);
    memory_allocator = nullptr;
    return halide_error_code_success;
}

// --

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_MEMORY_H
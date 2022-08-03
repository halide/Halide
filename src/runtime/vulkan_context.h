#ifndef HALIDE_RUNTIME_VULKAN_CONTEXT_H
#define HALIDE_RUNTIME_VULKAN_CONTEXT_H

#include "printer.h"
#include "runtime_internal.h"
#include "scoped_spin_lock.h"

#include "vulkan_extensions.h"
#include "vulkan_internal.h"
#include "vulkan_memory.h"

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

// An Vulkan context/queue/synchronization lock defined in this module with weak linkage
// Vulkan Memory allocator for host-device allocations
halide_vulkan_memory_allocator *WEAK cached_allocator = nullptr;
VkInstance WEAK cached_instance = nullptr;
VkDevice WEAK cached_device = nullptr;
VkCommandPool WEAK cached_command_pool = 0;
VkQueue WEAK cached_queue = nullptr;
VkPhysicalDevice WEAK cached_physical_device = nullptr;
uint32_t WEAK cached_queue_family_index = 0;
volatile ScopedSpinLock::AtomicFlag WEAK thread_lock = 0;

// --------------------------------------------------------------------------

// Helper object to acquire and release the Vulkan context.
class VulkanContext {
    void *user_context;

public:
    VulkanMemoryAllocator *allocator;
    VkInstance instance;
    VkDevice device;
    VkCommandPool command_pool;
    VkPhysicalDevice physical_device;
    VkQueue queue;
    uint32_t queue_family_index;  // used for operations requiring queue family
    VkResult error;

    HALIDE_ALWAYS_INLINE VulkanContext(void *user_context)
        : user_context(user_context),
          allocator(nullptr),
          instance(nullptr),
          device(nullptr),
          command_pool(0),
          physical_device(nullptr),
          queue(nullptr),
          queue_family_index(0),
          error(VK_SUCCESS) {

        int result = halide_vulkan_acquire_context(user_context,
                                                   reinterpret_cast<halide_vulkan_memory_allocator **>(&allocator),
                                                   &instance, &device, &physical_device, &command_pool, &queue, &queue_family_index);
        halide_abort_if_false(user_context, result == 0);
        halide_abort_if_false(user_context, allocator != nullptr);
        halide_abort_if_false(user_context, instance != nullptr);
        halide_abort_if_false(user_context, device != nullptr);
        halide_abort_if_false(user_context, command_pool != 0);
        halide_abort_if_false(user_context, queue != nullptr);
        halide_abort_if_false(user_context, physical_device != nullptr);
    }

    HALIDE_ALWAYS_INLINE ~VulkanContext() {
        halide_vulkan_release_context(user_context, instance, device, queue);
    }

    // For now, this is always nullptr
    HALIDE_ALWAYS_INLINE const VkAllocationCallbacks *allocation_callbacks() {
        return nullptr;
    }
};

// --------------------------------------------------------------------------

namespace {
    
// Initializes the instance (used by the default vk_create_context)
int vk_create_instance(void *user_context, const StringTable &requested_layers, VkInstance *instance, const VkAllocationCallbacks *alloc_callbacks) {
    debug(user_context) << "    vk_create_instance (user_context: " << user_context << ")\n";

    StringTable required_instance_extensions;
    vk_get_required_instance_extensions(user_context, required_instance_extensions);

    StringTable supported_instance_extensions;
    vk_get_supported_instance_extensions(user_context, supported_instance_extensions);

    bool valid_instance = vk_validate_required_extension_support(user_context, required_instance_extensions, supported_instance_extensions);
    halide_abort_if_false(user_context, valid_instance);

    debug(user_context) << "Vulkan: Found " << (uint32_t)required_instance_extensions.size() << " required extensions for instance!\n";
    for (int n = 0; n < (int)required_instance_extensions.size(); ++n) {
        debug(user_context) << "    extension: " << required_instance_extensions[n] << "\n";
    }

    VkApplicationInfo app_info = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,                                                        // struct type
        nullptr,                                                                                   // Next
        "Runtime",                                                                                 // application name
        VK_MAKE_API_VERSION(0, 1, 0, 0),                                                           // app version
        "Halide",                                                                                  // engine name
        VK_MAKE_API_VERSION(0, HALIDE_VERSION_MAJOR, HALIDE_VERSION_MINOR, HALIDE_VERSION_PATCH),  // engine version
        VK_API_VERSION_1_0};

    VkInstanceCreateInfo create_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,                                                                            // Next
        0,                                                                                  // Flags
        &app_info,                                                                          // ApplicationInfo
        (uint32_t)requested_layers.size(), requested_layers.data(),                         // Layers
        (uint32_t)required_instance_extensions.size(), required_instance_extensions.data()  // Extensions
    };

    VkResult result = vkCreateInstance(&create_info, alloc_callbacks, instance);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateInstance failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_incompatible_device_interface;
    }

    return halide_error_code_success;
}

int vk_select_device_for_context(void *user_context,
                                      VkInstance *instance, VkDevice *device,
                                      VkPhysicalDevice *physical_device,
                                      uint32_t *queue_family_index) {

    // For now handle more than 16 devices by just looking at the first 16.
    VkPhysicalDevice chosen_device = nullptr;
    VkPhysicalDevice avail_devices[16];
    uint32_t device_count = sizeof(avail_devices) / sizeof(avail_devices[0]);
    VkResult result = vkEnumeratePhysicalDevices(*instance, &device_count, avail_devices);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        debug(user_context) << "Vulkan: vkEnumeratePhysicalDevices failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_incompatible_device_interface;
    }

    if (device_count == 0) {
        debug(user_context) << "Vulkan: No devices found.\n";
        return halide_error_code_incompatible_device_interface;
    }

    const char *dev_type = halide_vulkan_get_device_type(user_context);

    // Try to find a device that supports compute.
    uint32_t queue_family = 0;
    for (uint32_t i = 0; (chosen_device == nullptr) && (i < device_count); i++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(avail_devices[i], &properties);
        debug(user_context) << "Vulkan: Checking device #" << i << "='" << properties.deviceName << "'\n";

        int matching_device = 0;
        if ((dev_type != nullptr) && (*dev_type != '\0')) {
            if (strstr(dev_type, "cpu") && (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)) {
                matching_device = 1;
            } else if (strstr(dev_type, "integrated-gpu") && ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU))) {
                matching_device = 1;
            } else if (strstr(dev_type, "discrete-gpu") && ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))) {
                matching_device = 1;
            } else if (strstr(dev_type, "virtual-gpu") && (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)) {
                matching_device = 1;
            } else if (strstr(dev_type, "gpu") && ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) || (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU))) {
                matching_device = 1;
            }
        } else {
            // use a non-virtual gpu device by default
            if ((properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) ||
                (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) {
                matching_device = 1;
            }
        }

        if (matching_device) {
            VkQueueFamilyProperties queue_properties[16];
            uint32_t queue_properties_count = sizeof(queue_properties) / sizeof(queue_properties[0]);
            vkGetPhysicalDeviceQueueFamilyProperties(avail_devices[i], &queue_properties_count, queue_properties);
            for (uint32_t j = 0; (chosen_device == nullptr) && (j < queue_properties_count); j++) {
                if (queue_properties[j].queueCount > 0 &&
                    queue_properties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    chosen_device = avail_devices[i];
                    queue_family = j;

                    debug(user_context) << "Vulkan: Found matching compute device '" << properties.deviceName << "'\n";
                }
            }
        }
    }
    // If nothing, just try the first one for now.
    if (chosen_device == nullptr) {
        queue_family = 0;
        chosen_device = avail_devices[0];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(chosen_device, &properties);
        debug(user_context) << "Vulkan: Defaulting to first compute device '" << properties.deviceName << "'\n";
    }

    *queue_family_index = queue_family;
    *physical_device = chosen_device;
    return halide_error_code_success;
}

int vk_create_device(void *user_context, const StringTable &requested_layers, VkInstance *instance, VkDevice *device, VkQueue *queue,
                          VkPhysicalDevice *physical_device, uint32_t *queue_family_index, const VkAllocationCallbacks *alloc_callbacks) {

    StringTable required_device_extensions;
    vk_get_required_device_extensions(user_context, required_device_extensions);

    StringTable optional_device_extensions;
    vk_get_optional_device_extensions(user_context, optional_device_extensions);

    StringTable supported_device_extensions;
    vk_get_supported_device_extensions(user_context, *physical_device, supported_device_extensions);

    bool valid_device = vk_validate_required_extension_support(user_context, required_device_extensions, supported_device_extensions);
    halide_abort_if_false(user_context, valid_device);

    debug(user_context) << "Vulkan: Found " << (uint32_t)required_device_extensions.size() << " required extensions for device!\n";
    for (int n = 0; n < (int)required_device_extensions.size(); ++n) {
        debug(user_context) << "    required extension: " << required_device_extensions[n] << "\n";
    }

    // enable all available optional extensions
    debug(user_context) << "Vulkan: Found " << (uint32_t)optional_device_extensions.size() << " optional extensions for device!\n";
    for (int n = 0; n < (int)optional_device_extensions.size(); ++n) {
        if (supported_device_extensions.contains(optional_device_extensions[n])) {
            debug(user_context) << "    optional extension: " << optional_device_extensions[n] << "\n";
            required_device_extensions.append(user_context, optional_device_extensions[n]);
        }
    }

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo device_queue_create_info = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,  // Next
        0,        // Flags
        *queue_family_index,
        1,
        &queue_priority,
    };

    VkDeviceCreateInfo device_create_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,  // Next
        0,        // Flags
        1,        // Count of queues to create
        &device_queue_create_info,
        (uint32_t)requested_layers.size(), requested_layers.data(),                      // Layers
        (uint32_t)required_device_extensions.size(), required_device_extensions.data(),  // Enabled extensions
        nullptr,                                                                         // VkPhysicalDeviceFeatures
    };

    VkResult result = vkCreateDevice(*physical_device, &device_create_info, alloc_callbacks, device);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateDevice failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_incompatible_device_interface;
    }

    vkGetDeviceQueue(cached_device, *queue_family_index, 0, queue);
    return halide_error_code_success;
}

// Initializes the context (used by the default implementation of halide_acquire_context)
int vk_create_context(void *user_context, VulkanMemoryAllocator **allocator,
                           VkInstance *instance, VkDevice *device, VkPhysicalDevice *physical_device, 
                           VkCommandPool *command_pool, VkQueue *queue, uint32_t *queue_family_index) {

    debug(user_context) << "    vk_create_context (user_context: " << user_context << ")\n";

    StringTable requested_layers;
    uint32_t requested_layer_count = vk_get_requested_layers(user_context, requested_layers);
    debug(user_context) << "Vulkan: Requested " << requested_layer_count << " layers for instance!\n";
    for (int n = 0; n < (int)requested_layer_count; ++n) {
        debug(user_context) << "    layer: " << requested_layers[n] << "\n";
    }

    const VkAllocationCallbacks *alloc_callbacks = halide_vulkan_get_allocation_callbacks(user_context);
    int status = vk_create_instance(user_context, requested_layers, instance, alloc_callbacks);
    if (status != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create instance for context!\n";
        return halide_error_code_generic_error;
    }

    if (vkCreateDevice == nullptr) {
        vk_load_vulkan_functions(*instance);
    }

    status = vk_select_device_for_context(user_context, instance, device, physical_device, queue_family_index);
    if (status != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to select device for context!\n";
        return halide_error_code_generic_error;
    }

    status = vk_create_device(user_context, requested_layers, instance, device, queue, physical_device, queue_family_index, alloc_callbacks);
    if (status != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create device for context!\n";
        return halide_error_code_generic_error;
    }

    *allocator = vk_create_memory_allocator(user_context, *device, *physical_device, alloc_callbacks);
    if (*allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create memory allocator for device!\n";
        return halide_error_code_generic_error;
    }

    VkResult result = vk_create_command_pool(user_context, *allocator, *queue_family_index, command_pool);
    if (result != VK_SUCCESS) {
        error(user_context) << "Vulkan: Failed to create command pool for context! Error: " << vk_get_error_name(result) << "\n";
        return result;
    }

    return halide_error_code_success;
}

// --------------------------------------------------------------------------

}  // namespace: (anonymous)
}  // namespace: Vulkan
}  // namespace: Internal
}  // namespace: Runtime
}  // namespace: Halide

#endif  /// HALIDE_RUNTIME_VULKAN_CONTEXT_H

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

// Vulkan Memory allocator for host-device allocations
halide_vulkan_memory_allocator *WEAK cached_allocator = nullptr;

// Cached instance related handles for device resources
VkInstance WEAK cached_instance = nullptr;
VkDevice WEAK cached_device = nullptr;
VkCommandPool WEAK cached_command_pool = 0;
VkQueue WEAK cached_queue = nullptr;
VkPhysicalDevice WEAK cached_physical_device = nullptr;
uint32_t WEAK cached_queue_family_index = 0;

// A Vulkan context/queue/synchronization lock defined in this module with weak linkage
volatile ScopedSpinLock::AtomicFlag WEAK thread_lock = 0;

// --------------------------------------------------------------------------

// Helper object to acquire and release the Vulkan context.
class VulkanContext {
    void *user_context;

public:
    VulkanMemoryAllocator *allocator = nullptr;
    VkInstance instance = nullptr;
    VkDevice device = nullptr;
    VkCommandPool command_pool = 0;
    VkPhysicalDevice physical_device = nullptr;
    VkQueue queue = nullptr;
    uint32_t queue_family_index = 0;  // used for operations requiring queue family
    halide_error_code_t error = halide_error_code_success;

    HALIDE_ALWAYS_INLINE explicit VulkanContext(void *user_context)
        : user_context(user_context) {

        int result = halide_vulkan_acquire_context(user_context,
                                                   reinterpret_cast<halide_vulkan_memory_allocator **>(&allocator),
                                                   &instance, &device, &physical_device, &command_pool, &queue, &queue_family_index);
        if (result != halide_error_code_success) {
            error = halide_error_code_device_interface_no_device;
            halide_error_no_device_interface(user_context);
        }
        halide_debug_assert(user_context, allocator != nullptr);
        halide_debug_assert(user_context, instance != nullptr);
        halide_debug_assert(user_context, device != nullptr);
        halide_debug_assert(user_context, command_pool != 0);
        halide_debug_assert(user_context, queue != nullptr);
        halide_debug_assert(user_context, physical_device != nullptr);
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

int vk_find_compute_capability(void *user_context, int *major, int *minor) {
    debug(user_context) << " vk_find_compute_capability (user_context: " << user_context << ")\n";

    VkInstance instance = nullptr;
    VkDevice device = nullptr;
    VkPhysicalDevice physical_device = nullptr;
    uint32_t queue_family_index = 0;

    StringTable requested_layers;
    vk_get_requested_layers(user_context, requested_layers);

    const VkAllocationCallbacks *alloc_callbacks = halide_vulkan_get_allocation_callbacks(user_context);
    int status = vk_create_instance(user_context, requested_layers, &instance, alloc_callbacks);
    if (status != halide_error_code_success) {
        debug(user_context) << "  no valid vulkan runtime was found ...\n";
        *major = 0;
        *minor = 0;
        return 0;
    }

    if (vkCreateDevice == nullptr) {
        vk_load_vulkan_functions(instance);
    }

    status = vk_select_device_for_context(user_context, &instance, &device, &physical_device, &queue_family_index);
    if (status != halide_error_code_success) {
        debug(user_context) << "  no valid vulkan device was found ...\n";
        *major = 0;
        *minor = 0;
        return 0;
    }

    VkPhysicalDeviceProperties device_properties = {0};
    debug(user_context) << "  querying for device properties ...\n";
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    *major = VK_API_VERSION_MAJOR(device_properties.apiVersion);
    *minor = VK_API_VERSION_MINOR(device_properties.apiVersion);
    debug(user_context) << "  found device compute capability v" << *major << "." << *minor << " ...\n";

    vk_destroy_instance(user_context, instance, alloc_callbacks);
    return 0;
}

// Initializes the instance (used by the default vk_create_context)
int vk_create_instance(void *user_context, const StringTable &requested_layers, VkInstance *instance, const VkAllocationCallbacks *alloc_callbacks) {
    debug(user_context) << " vk_create_instance (user_context: " << user_context << ")\n";

    StringTable required_instance_extensions;
    vk_get_required_instance_extensions(user_context, required_instance_extensions);

    StringTable supported_instance_extensions;
    vk_get_supported_instance_extensions(user_context, supported_instance_extensions);

    bool valid_instance = vk_validate_required_extension_support(user_context, required_instance_extensions, supported_instance_extensions);
    halide_abort_if_false(user_context, valid_instance);

    debug(user_context) << "  found " << (uint32_t)required_instance_extensions.size() << " required extensions for instance!\n";
    for (int n = 0; n < (int)required_instance_extensions.size(); ++n) {
        debug(user_context) << "  extension: " << required_instance_extensions[n] << "\n";
    }

    // If we're running under Molten VK, we must enable the portability extension and create flags
    // to allow non-physical devices that are emulated to appear in the device list.
    uint32_t create_flags = 0;
    if (supported_instance_extensions.contains("VK_KHR_portability_enumeration") &&
        supported_instance_extensions.contains("VK_MVK_macos_surface")) {
        create_flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        required_instance_extensions.append(user_context, "VK_KHR_portability_enumeration");
    }

    VkApplicationInfo app_info = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,                                                        // struct type
        nullptr,                                                                                   // Next
        "Runtime",                                                                                 // application name
        VK_MAKE_API_VERSION(0, 1, 0, 0),                                                           // app version
        "Halide",                                                                                  // engine name
        VK_MAKE_API_VERSION(0, HALIDE_VERSION_MAJOR, HALIDE_VERSION_MINOR, HALIDE_VERSION_PATCH),  // engine version
        VK_API_VERSION_1_3};                                                                       // FIXME: only use the minimum capability necessary

    VkInstanceCreateInfo create_info = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,                                                                            // Next
        create_flags,                                                                       // Flags
        &app_info,                                                                          // ApplicationInfo
        (uint32_t)requested_layers.size(), requested_layers.data(),                         // Layers
        (uint32_t)required_instance_extensions.size(), required_instance_extensions.data()  // Extensions
    };

    VkResult result = vkCreateInstance(&create_info, alloc_callbacks, instance);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateInstance failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_device_interface_no_device;
    }

    return halide_error_code_success;
}

int vk_destroy_instance(void *user_context, VkInstance instance, const VkAllocationCallbacks *alloc_callbacks) {
    debug(user_context) << " vk_destroy_instance (user_context: " << user_context << ")\n";
    vkDestroyInstance(instance, alloc_callbacks);
    return halide_error_code_success;
}

int vk_select_device_for_context(void *user_context,
                                 VkInstance *instance, VkDevice *device,
                                 VkPhysicalDevice *physical_device,
                                 uint32_t *queue_family_index) {
    // query for the number of physical devices available in this instance
    uint32_t device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(*instance, &device_count, nullptr);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        debug(user_context) << "Vulkan: vkEnumeratePhysicalDevices failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_device_interface_no_device;
    }
    if (device_count == 0) {
        debug(user_context) << "Vulkan: No devices found.\n";
        return halide_error_code_device_interface_no_device;
    }

    // allocate enough storage for the physical device query results
    BlockStorage::Config device_query_storage_config;
    device_query_storage_config.entry_size = sizeof(VkPhysicalDevice);
    BlockStorage device_query_storage(user_context, device_query_storage_config);
    device_query_storage.resize(user_context, device_count);

    VkPhysicalDevice chosen_device = nullptr;
    VkPhysicalDevice *avail_devices = (VkPhysicalDevice *)(device_query_storage.data());
    if (avail_devices == nullptr) {
        debug(user_context) << "Vulkan: Out of system memory!\n";
        return halide_error_code_out_of_memory;
    }
    result = vkEnumeratePhysicalDevices(*instance, &device_count, avail_devices);
    if ((result != VK_SUCCESS) && (result != VK_INCOMPLETE)) {
        debug(user_context) << "Vulkan: vkEnumeratePhysicalDevices failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_device_interface_no_device;
    }

    // get the configurable device type to search for (e.g. 'cpu', 'gpu', 'integrated-gpu', 'discrete-gpu', ...)
    const char *dev_type = halide_vulkan_get_device_type(user_context);

    // try to find a matching device that supports compute.
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
            // get the number of supported queues for this physical device
            uint32_t queue_properties_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(avail_devices[i], &queue_properties_count, nullptr);
            if (queue_properties_count < 1) {
                continue;
            }

            // allocate enough storage for the queue properties query results
            BlockStorage::Config queue_properties_storage_config;
            queue_properties_storage_config.entry_size = sizeof(VkPhysicalDevice);
            BlockStorage queue_properties_storage(user_context, queue_properties_storage_config);
            queue_properties_storage.resize(user_context, queue_properties_count);

            VkQueueFamilyProperties *queue_properties = (VkQueueFamilyProperties *)(queue_properties_storage.data());
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
    debug(user_context) << " vk_create_device (user_context=" << user_context << ")\n";

    debug(user_context) << "  checking for required device extensions ...\n";
    StringTable required_device_extensions;
    vk_get_required_device_extensions(user_context, required_device_extensions);

    debug(user_context) << "  checking for optional device extensions ...\n";
    StringTable optional_device_extensions;
    vk_get_optional_device_extensions(user_context, optional_device_extensions);

    debug(user_context) << "  validating supported device extensions ...\n";
    StringTable supported_device_extensions;
    vk_get_supported_device_extensions(user_context, *physical_device, supported_device_extensions);

    bool valid_device = vk_validate_required_extension_support(user_context, required_device_extensions, supported_device_extensions);
    if (!valid_device) {
        debug(user_context) << "Vulkan: Unable to validate required extension support!\n";
        return halide_error_code_device_interface_no_device;
    }

    debug(user_context) << "  found " << (uint32_t)required_device_extensions.size() << " required extensions for device!\n";
    for (uint32_t n = 0; n < required_device_extensions.size(); ++n) {
        debug(user_context) << "   required extension: " << required_device_extensions[n] << "\n";
    }

    // enable all available optional extensions
    debug(user_context) << "  checking for " << (uint32_t)optional_device_extensions.size() << " optional extensions for device ...\n";
    for (uint32_t n = 0; n < optional_device_extensions.size(); ++n) {
        if (supported_device_extensions.contains(optional_device_extensions[n])) {
            debug(user_context) << "   optional extension: " << optional_device_extensions[n] << "\n";
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

    // Get the API version to determine what device features are valid to search for
    VkPhysicalDeviceProperties device_properties = {0};
    debug(user_context) << "  querying for device properties ...\n";
    vkGetPhysicalDeviceProperties(*physical_device, &device_properties);
    uint32_t major_version = VK_API_VERSION_MAJOR(device_properties.apiVersion);
    uint32_t minor_version = VK_API_VERSION_MINOR(device_properties.apiVersion);
    bool has_capability_v11 = (major_version >= 1) && (minor_version >= 1);  // supports >= v1.1
    bool has_capability_v12 = (major_version >= 1) && (minor_version >= 2);  // supports >= v1.2
    debug(user_context) << "  found device compute capability v" << major_version << "." << minor_version << " ...\n";

    // Get the device features so that all supported features are enabled when device is created
    VkPhysicalDeviceFeatures device_features = {};
    void *extended_features_ptr = nullptr;
    void *standard_features_ptr = nullptr;

    debug(user_context) << "  querying for device features...\n";
    vkGetPhysicalDeviceFeatures(*physical_device, &device_features);
    debug(user_context) << "   shader float64 support: " << (device_features.shaderFloat64 ? "true" : "false") << "...\n";
    debug(user_context) << "   shader int64 support: " << (device_features.shaderInt64 ? "true" : "false") << "...\n";
    debug(user_context) << "   shader int16 support: " << (device_features.shaderInt16 ? "true" : "false") << "...\n";

    // assemble the chain of features to query, but only add the ones that exist in the API version

    // note: requires v1.2+
    VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shader_f16_i8_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,
                                                                      nullptr, VK_FALSE, VK_FALSE};

    // note: requires v1.2+
    VkPhysicalDevice8BitStorageFeaturesKHR storage_8bit_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR,
                                                               &shader_f16_i8_ext, VK_FALSE, VK_FALSE, VK_FALSE};

    // note: requires v1.1+
    VkPhysicalDevice16BitStorageFeaturesKHR storage_16bit_ext = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,
                                                                 (has_capability_v12 ? &storage_8bit_ext : nullptr),
                                                                 VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE};

    VkPhysicalDeviceFeatures2KHR device_features_ext = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
        &storage_16bit_ext, device_features};

    // Look for extended device feature query method (KHR was removed when it was adopted into v1.1+)
    PFN_vkGetPhysicalDeviceFeatures2KHR vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(*instance, "vkGetPhysicalDeviceFeatures2KHR");  // v1.0+
    if (!vkGetPhysicalDeviceFeatures2KHR) {
        vkGetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(*instance, "vkGetPhysicalDeviceFeatures2");
    }

    // If the instance runtime supports querying extended device features, request them
    if (vkGetPhysicalDeviceFeatures2KHR && has_capability_v11) {

        debug(user_context) << "  querying for extended device features...\n";
        vkGetPhysicalDeviceFeatures2KHR(*physical_device, &device_features_ext);
        debug(user_context) << "   shader int8 support: " << (shader_f16_i8_ext.shaderInt8 ? "true" : "false") << "...\n";
        debug(user_context) << "   shader float16 support: " << (shader_f16_i8_ext.shaderFloat16 ? "true" : "false") << "...\n";
        if (has_capability_v12) {
            debug(user_context) << "   storage buffer 8bit access support: " << (storage_8bit_ext.storageBuffer8BitAccess ? "true" : "false") << "...\n";
            debug(user_context) << "   storage buffer 16bit access support: " << (storage_16bit_ext.storageBuffer16BitAccess ? "true" : "false") << "...\n";
        }
        extended_features_ptr = (void *)(&device_features_ext);  // pass extended features (which also contains the standard features)
    } else {
        standard_features_ptr = &device_features;  // pass v1.0 standard features
    }

    VkDeviceCreateInfo device_create_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        extended_features_ptr,  // Extended struct ptr (used here for requesting chain of extended features)
        0,                      // Flags
        1,                      // Count of queues to create
        &device_queue_create_info,
        (uint32_t)requested_layers.size(), requested_layers.data(),                      // Layers
        (uint32_t)required_device_extensions.size(), required_device_extensions.data(),  // Enabled extensions
        (VkPhysicalDeviceFeatures *)standard_features_ptr,                               // Requested device features
    };

    VkResult result = vkCreateDevice(*physical_device, &device_create_info, alloc_callbacks, device);
    if (result != VK_SUCCESS) {
        debug(user_context) << "Vulkan: vkCreateDevice failed with return code: " << vk_get_error_name(result) << "\n";
        return halide_error_code_device_interface_no_device;
    }

    vkGetDeviceQueue(cached_device, *queue_family_index, 0, queue);
    return halide_error_code_success;
}

// Initializes the context (used by the default implementation of halide_acquire_context)
int vk_create_context(void *user_context, VulkanMemoryAllocator **allocator,
                      VkInstance *instance, VkDevice *device, VkPhysicalDevice *physical_device,
                      VkCommandPool *command_pool, VkQueue *queue, uint32_t *queue_family_index) {

    debug(user_context) << " vk_create_context (user_context: " << user_context << ")\n";

    StringTable requested_layers;
    uint32_t requested_layer_count = vk_get_requested_layers(user_context, requested_layers);
    debug(user_context) << "  requested " << requested_layer_count << " layers for instance!\n";
    for (int n = 0; n < (int)requested_layer_count; ++n) {
        debug(user_context) << "   layer: " << requested_layers[n] << "\n";
    }

    const VkAllocationCallbacks *alloc_callbacks = halide_vulkan_get_allocation_callbacks(user_context);
    int error_code = vk_create_instance(user_context, requested_layers, instance, alloc_callbacks);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create instance for context!\n";
        return error_code;
    }

    if (vkCreateDevice == nullptr) {
        vk_load_vulkan_functions(*instance);
    }

    error_code = vk_select_device_for_context(user_context, instance, device, physical_device, queue_family_index);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to select device for context!\n";
        return error_code;
    }

    error_code = vk_create_device(user_context, requested_layers, instance, device, queue, physical_device, queue_family_index, alloc_callbacks);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create device for context!\n";
        return error_code;
    }

    *allocator = vk_create_memory_allocator(user_context, *device, *physical_device, alloc_callbacks);
    if (*allocator == nullptr) {
        error(user_context) << "Vulkan: Failed to create memory allocator for device!\n";
        return halide_error_code_generic_error;
    }

    error_code = vk_create_command_pool(user_context, *allocator, *queue_family_index, command_pool);
    if (error_code != halide_error_code_success) {
        error(user_context) << "Vulkan: Failed to create command pool for context!\n";
        return error_code;
    }

    return halide_error_code_success;
}

// --------------------------------------------------------------------------

}  // namespace
}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  /// HALIDE_RUNTIME_VULKAN_CONTEXT_H

#ifndef HALIDE_RUNTIME_VULKAN_EXTENSIONS_H
#define HALIDE_RUNTIME_VULKAN_EXTENSIONS_H

#include "vulkan_internal.h"

// --------------------------------------------------------------------------

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

WEAK char layer_names[1024];
WEAK ScopedSpinLock::AtomicFlag layer_names_lock = 0;
WEAK bool layer_names_initialized = false;

WEAK char extension_names[1024];
WEAK ScopedSpinLock::AtomicFlag extension_names_lock = 0;
WEAK bool extension_names_initialized = false;

WEAK char device_type[256];
WEAK ScopedSpinLock::AtomicFlag device_type_lock = 0;
WEAK bool device_type_initialized = false;

WEAK char build_options[1024];
WEAK ScopedSpinLock::AtomicFlag build_options_lock = 0;
WEAK bool build_options_initialized = false;

WEAK char alloc_config[1024];
WEAK ScopedSpinLock::AtomicFlag alloc_config_lock = 0;
WEAK bool alloc_config_initialized = false;

// --------------------------------------------------------------------------
namespace {

void vk_set_layer_names_internal(const char *n) {
    if (n) {
        size_t buffer_size = sizeof(layer_names) / sizeof(layer_names[0]);
        StringUtils::copy_up_to(layer_names, n, buffer_size);
    } else {
        layer_names[0] = 0;
    }
    layer_names_initialized = true;
}

const char *vk_get_layer_names_internal(void *user_context) {
    if (!layer_names_initialized) {
        const char *value = getenv("HL_VK_LAYERS");
        if (value == nullptr) {
            value = getenv("VK_INSTANCE_LAYERS");
        }
        vk_set_layer_names_internal(value);
    }
    return layer_names;
}

void vk_set_extension_names_internal(const char *n) {
    if (n) {
        size_t buffer_size = sizeof(extension_names) / sizeof(extension_names[0]);
        StringUtils::copy_up_to(extension_names, n, buffer_size);
    } else {
        extension_names[0] = 0;
    }
    extension_names_initialized = true;
}

const char *vk_get_extension_names_internal(void *user_context) {
    if (!extension_names_initialized) {
        const char *name = getenv("HL_VK_EXTENSIONS");
        vk_set_extension_names_internal(name);
    }
    return extension_names;
}

void vk_set_device_type_internal(const char *n) {
    if (n) {
        size_t buffer_size = sizeof(device_type) / sizeof(device_type[0]);
        StringUtils::copy_up_to(device_type, n, buffer_size);
    } else {
        device_type[0] = 0;
    }
    device_type_initialized = true;
}

const char *vk_get_device_type_internal(void *user_context) {
    if (!device_type_initialized) {
        const char *name = getenv("HL_VK_DEVICE_TYPE");
        vk_set_device_type_internal(name);
    }
    return device_type;
}

void vk_set_build_options_internal(const char *n) {
    if (n) {
        size_t buffer_size = sizeof(build_options) / sizeof(build_options[0]);
        StringUtils::copy_up_to(build_options, n, buffer_size);
    } else {
        build_options[0] = 0;
    }
    build_options_initialized = true;
}

const char *vk_get_build_options_internal(void *user_context) {
    if (!build_options_initialized) {
        const char *name = getenv("HL_VK_BUILD_OPTIONS");
        vk_set_build_options_internal(name);
    }
    return build_options;
}

void vk_set_alloc_config_internal(const char *n) {
    if (n) {
        size_t buffer_size = sizeof(alloc_config) / sizeof(alloc_config[0]);
        StringUtils::copy_up_to(alloc_config, n, buffer_size);
    } else {
        alloc_config[0] = 0;
    }
    alloc_config_initialized = true;
}

const char *vk_get_alloc_config_internal(void *user_context) {
    if (!alloc_config_initialized) {
        const char *name = getenv("HL_VK_ALLOC_CONFIG");
        vk_set_alloc_config_internal(name);
    }
    return alloc_config;
}

// --------------------------------------------------------------------------

uint32_t vk_get_requested_layers(void *user_context, StringTable &layer_table) {
    ScopedSpinLock lock(&layer_names_lock);
    const char *layer_names = vk_get_layer_names_internal(user_context);
    return layer_table.parse(user_context, layer_names, HL_VK_ENV_DELIM);
}

uint32_t vk_get_required_instance_extensions(void *user_context, StringTable &ext_table) {
    const char *required_ext_table[] = {"VK_KHR_get_physical_device_properties2"};
    const uint32_t required_ext_count = sizeof(required_ext_table) / sizeof(required_ext_table[0]);
    ext_table.fill(user_context, (const char **)required_ext_table, required_ext_count);
    return required_ext_count;
}

uint32_t vk_get_supported_instance_extensions(void *user_context, StringTable &ext_table) {

    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties");

    if (vkEnumerateInstanceExtensionProperties == nullptr) {
        debug(user_context) << "Vulkan: Missing vkEnumerateInstanceExtensionProperties proc address! Invalid loader?!\n";
        return 0;
    }

    debug(user_context) << "Vulkan: Checking vkEnumerateInstanceExtensionProperties for extensions ...\n";

    uint32_t avail_ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &avail_ext_count, nullptr);

    if (avail_ext_count) {
        BlockStorage::Config config;
        config.entry_size = sizeof(VkExtensionProperties);
        config.minimum_capacity = avail_ext_count;

        BlockStorage extension_properties(user_context, config);
        extension_properties.resize(user_context, avail_ext_count);

        vkEnumerateInstanceExtensionProperties(nullptr,
                                               &avail_ext_count, static_cast<VkExtensionProperties *>(extension_properties.data()));

        for (uint32_t n = 0; n < avail_ext_count; ++n) {
            const VkExtensionProperties *properties = static_cast<const VkExtensionProperties *>(extension_properties[n]);
            debug(user_context) << "    [" << n << "]: " << properties->extensionName << "\n";
        }

        ext_table.resize(user_context, avail_ext_count);
        for (uint32_t n = 0; n < avail_ext_count; ++n) {
            const VkExtensionProperties *properties = static_cast<const VkExtensionProperties *>(extension_properties[n]);
            ext_table.assign(user_context, n, properties->extensionName);
        }
    }
    debug(user_context) << "Vulkan: vkEnumerateInstanceExtensionProperties found  " << avail_ext_count << " extensions ...\n";
    return avail_ext_count;
}

uint32_t vk_get_required_device_extensions(void *user_context, StringTable &ext_table) {
    const char *required_ext_table[] = {"VK_KHR_8bit_storage", "VK_KHR_storage_buffer_storage_class"};
    const uint32_t required_ext_count = sizeof(required_ext_table) / sizeof(required_ext_table[0]);
    ext_table.fill(user_context, (const char **)required_ext_table, required_ext_count);
    return required_ext_count;
}

uint32_t vk_get_optional_device_extensions(void *user_context, StringTable &ext_table) {
    const char *optional_ext_table[] = {
        "VK_KHR_portability_subset",  //< necessary for running under Molten (aka Vulkan on Mac)
        "VK_KHR_16bit_storage",
        "VK_KHR_shader_float16_int8",
        "VK_KHR_shader_float_controls"};
    const uint32_t optional_ext_count = sizeof(optional_ext_table) / sizeof(optional_ext_table[0]);
    ext_table.fill(user_context, (const char **)optional_ext_table, optional_ext_count);
    return optional_ext_count;
}

uint32_t vk_get_supported_device_extensions(void *user_context, VkPhysicalDevice physical_device, StringTable &ext_table) {
    debug(user_context) << "vk_get_supported_device_extensions\n";
    if (vkEnumerateDeviceExtensionProperties == nullptr) {
        debug(user_context) << "Vulkan: Missing vkEnumerateDeviceExtensionProperties proc address! Invalid loader?!\n";
        return 0;
    }

    debug(user_context) << "Vulkan: Checking vkEnumerateDeviceExtensionProperties for extensions ...\n";

    uint32_t avail_ext_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &avail_ext_count, nullptr);
    if (avail_ext_count > 0) {
        BlockStorage::Config config;
        config.entry_size = sizeof(VkExtensionProperties);
        config.minimum_capacity = avail_ext_count;

        BlockStorage extension_properties(user_context, config);
        extension_properties.resize(user_context, avail_ext_count);

        vkEnumerateDeviceExtensionProperties(physical_device, nullptr,
                                             &avail_ext_count, static_cast<VkExtensionProperties *>(extension_properties.data()));

        for (uint32_t n = 0; n < avail_ext_count; ++n) {
            const VkExtensionProperties *properties = static_cast<const VkExtensionProperties *>(extension_properties[n]);
            debug(user_context) << "    [" << n << "]: " << properties->extensionName << "\n";
        }

        ext_table.resize(user_context, avail_ext_count);
        for (uint32_t n = 0; n < avail_ext_count; ++n) {
            const VkExtensionProperties *properties = static_cast<const VkExtensionProperties *>(extension_properties[n]);
            ext_table.assign(user_context, n, properties->extensionName);
        }
    }

    debug(user_context) << "Vulkan: vkEnumerateDeviceExtensionProperties found  " << avail_ext_count << " extensions ...\n";
    return avail_ext_count;
}

bool vk_validate_required_extension_support(void *user_context,
                                            const StringTable &required_extensions,
                                            const StringTable &supported_extensions) {
    debug(user_context) << "Vulkan: Validating " << uint32_t(required_extensions.size()) << " extensions ...\n";
    bool validated = true;
    for (uint32_t n = 0; n < required_extensions.size(); ++n) {
        const char *extension = required_extensions[n];
        if (!supported_extensions.contains(extension)) {
            debug(user_context) << "Vulkan: Missing required extension: '" << extension << "'!\n";
            validated = false;
        }
    }
    return validated;
}

// --------------------------------------------------------------------------

}  // namespace
}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

// --------------------------------------------------------------------------

using namespace Halide::Runtime::Internal::Vulkan;

// --------------------------------------------------------------------------

extern "C" {

// --------------------------------------------------------------------------

WEAK void halide_vulkan_set_layer_names(const char *n) {
    ScopedSpinLock lock(&layer_names_lock);
    vk_set_layer_names_internal(n);
}

WEAK const char *halide_vulkan_get_layer_names(void *user_context) {
    ScopedSpinLock lock(&layer_names_lock);
    return vk_get_layer_names_internal(user_context);
}

WEAK void halide_vulkan_set_extension_names(const char *n) {
    ScopedSpinLock lock(&extension_names_lock);
    vk_set_extension_names_internal(n);
}

WEAK const char *halide_vulkan_get_extension_names(void *user_context) {
    ScopedSpinLock lock(&extension_names_lock);
    return vk_get_extension_names_internal(user_context);
}

WEAK void halide_vulkan_set_device_type(const char *n) {
    ScopedSpinLock lock(&device_type_lock);
    vk_set_device_type_internal(n);
}

WEAK const char *halide_vulkan_get_device_type(void *user_context) {
    ScopedSpinLock lock(&device_type_lock);
    return vk_get_device_type_internal(user_context);
}

WEAK void halide_vulkan_set_build_options(const char *n) {
    ScopedSpinLock lock(&build_options_lock);
    vk_set_build_options_internal(n);
}

WEAK const char *halide_vulkan_get_build_options(void *user_context) {
    ScopedSpinLock lock(&build_options_lock);
    return vk_get_build_options_internal(user_context);
}

WEAK void halide_vulkan_set_alloc_config(const char *n) {
    ScopedSpinLock lock(&alloc_config_lock);
    vk_set_alloc_config_internal(n);
}

WEAK const char *halide_vulkan_get_alloc_config(void *user_context) {
    ScopedSpinLock lock(&alloc_config_lock);
    return vk_get_alloc_config_internal(user_context);
}

// --------------------------------------------------------------------------

}  // extern "C"

#endif  // HALIDE_RUNTIME_VULKAN_EXTENSIONS_H
#ifndef HALIDE_RUNTIME_VULKAN_INTERFACE_H
#define HALIDE_RUNTIME_VULKAN_INTERFACE_H

#include "runtime_internal.h"

// --------------------------------------------------------------------------
// Vulkan Specific Definitions
// --------------------------------------------------------------------------

// Vulkan API version identifier macro
#define VK_MAKE_API_VERSION(variant, major, minor, patch) \
    ((((uint32_t)(variant)) << 29) | (((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))

// Vulkan API version 1.0.0
#define VK_API_VERSION_1_0 VK_MAKE_API_VERSION(0, 1, 0, 0)  // Patch version should always be set to 0

// Environment variable string delimiter
#ifdef WINDOWS
#define HL_VK_ENV_DELIM ";"
#else
#define HL_VK_ENV_DELIM ":"
#endif

// Prototypes for the subset of the Vulkan API we need
#define VK_NO_PROTOTYPES
// NOLINTNEXTLINE
#include "mini_vulkan.h"

// --------------------------------------------------------------------------
// Vulkan API Definition
// --------------------------------------------------------------------------

extern "C" WEAK void atexit(void (*fn)());
WEAK void halide_vulkan_cleanup();

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

// Halide device interface struct for runtime specific function table
extern WEAK halide_device_interface_t vulkan_device_interface;

// --------------------------------------------------------------------------

// The default implementation of halide_vulkan_get_symbol attempts to load
// the Vulkan loader shared library/DLL, and then get the symbol from it.
WEAK void *lib_vulkan = nullptr;

extern "C" WEAK void *halide_vulkan_get_symbol(void *user_context, const char *name) {
    // Only try to load the library if the library isn't already
    // loaded, or we can't load the symbol from the process already.
    void *symbol = halide_get_library_symbol(lib_vulkan, name);
    if (symbol) {
        return symbol;
    }

    const char *lib_names[] = {
#ifdef WINDOWS
        "vulkan-1.dll",
#else
        "libvulkan.so.1",
        "libvulkan.1.dylib",
#endif
    };
    for (auto &lib_name : lib_names) {
        lib_vulkan = halide_load_library(lib_name);
        if (lib_vulkan) {
            debug(user_context) << "    Loaded Vulkan loader library: " << lib_name << "\n";
            atexit(halide_vulkan_cleanup);
            break;
        } else {
            debug(user_context) << "    Missing Vulkan loader library: " << lib_name << "\n";
        }
    }

    return halide_get_library_symbol(lib_vulkan, name);
}

// Declare all the function pointers for the Vulkan API methods that will be resolved dynamically
// clang-format off
#define VULKAN_FN(fn) WEAK PFN_##fn fn; 
VULKAN_FN(vkCreateInstance)
VULKAN_FN(vkGetInstanceProcAddr)
#include "vulkan_functions.h"
#undef VULKAN_FN
// clang-format on

// Get the function pointers from the Vulkan loader to create an Instance (to find all available driver implementations)
void WEAK vk_load_vulkan_loader_functions(void *user_context) {
    debug(user_context) << "    vk_load_vulkan_loader_functions (user_context: " << user_context << ")\n";
#define VULKAN_FN(fn) fn = (PFN_##fn)halide_vulkan_get_symbol(user_context, #fn);
    VULKAN_FN(vkCreateInstance)
    VULKAN_FN(vkGetInstanceProcAddr)
#undef VULKAN_FN
}

// Get the function pointers from the Vulkan instance for the resolved driver API methods.
void WEAK vk_load_vulkan_functions(void *user_context, VkInstance instance) {
    debug(user_context) << "    vk_load_vulkan_functions (user_context: " << user_context << ")\n";
#define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn);
#include "vulkan_functions.h"
#undef VULKAN_FN
}

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERFACE_H

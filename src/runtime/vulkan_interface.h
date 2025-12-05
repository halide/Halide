#ifndef HALIDE_RUNTIME_VULKAN_INTERFACE_H
#define HALIDE_RUNTIME_VULKAN_INTERFACE_H

#include "runtime_internal.h"

// --------------------------------------------------------------------------
// Vulkan Specific Definitions
// --------------------------------------------------------------------------

// Environment variable string delimiter
#ifdef WINDOWS
#define HL_VK_ENV_DELIM ";"
#else
#define HL_VK_ENV_DELIM ":"
#endif

// Prototypes for the subset of the Vulkan API we need
#define VK_NO_PROTOTYPES
// NOLINTNEXTLINE
#include <vulkan/vulkan.h>

// --------------------------------------------------------------------------
// Vulkan API Definition
// --------------------------------------------------------------------------

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

    const char *env_libname = getenv("HL_VK_LOADER_LIB");
    if (env_libname) {
        lib_vulkan = halide_load_library(env_libname);
        if (lib_vulkan) {
            debug(user_context) << "    Loaded Vulkan loader library: " << env_libname << "\n";
        } else {
            debug(user_context) << "    Missing Vulkan loader library: " << env_libname << "\n";
        }
    }

    if(!lib_vulkan) {
        const char *lib_names[] = {
    #ifdef WINDOWS
            "vulkan-1.dll",
    #else
            "libvulkan.so.1", 
            "libvulkan.so",
            "libvulkan.dylib",
            "libvulkan.1.dylib",
            "/usr/local/lib/libvulkan.dylib", // MacOS ldopen doesn't search here by default
            "libMoltenVK.dylib",
            "vulkan.framework/vulkan", // Search for local frameworks (eg for IOS apps)
            "MoltenVK.framework/MoltenVK"
    #endif
        };
        for (auto &lib_name : lib_names) {
            lib_vulkan = halide_load_library(lib_name);
            if (lib_vulkan) {
                debug(user_context) << "    Loaded Vulkan loader library: " << lib_name << "\n";
                break;
            } else {
                debug(user_context) << "    Missing Vulkan loader library: " << lib_name << "\n";
            }
        }
    }

    if(!lib_vulkan) {
        print(user_context) << "WARNING: Could not find a Vulkan loader library!\n"
                            << "(Try setting the env var HL_VK_LOADER_LIB to an explicit path to fix this.)\n";
        return nullptr;
    }

    return halide_get_library_symbol(lib_vulkan, name);
}

// Declare all the function pointers for the Vulkan API methods that will be resolved dynamically

#define VULKAN_FN(fn) WEAK PFN_##fn fn = nullptr;
#define HL_USE_VULKAN_LOADER_FNS
#define HL_USE_VULKAN_INSTANCE_FNS
#define HL_USE_VULKAN_DEVICE_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_DEVICE_FNS
#undef HL_USE_VULKAN_INSTANCE_FNS
#undef HL_USE_VULKAN_LOADER_FNS
#undef VULKAN_FN

// Get the function pointers to the Vulkan loader (to find all available instances)
void WEAK vk_load_vulkan_loader_functions(void *user_context) {
    debug(user_context) << "    vk_load_vulkan_loader_functions (user_context: " << user_context << ")\n";
#define VULKAN_FN(fn) fn = (PFN_##fn)halide_vulkan_get_symbol(user_context, #fn);
#define HL_USE_VULKAN_LOADER_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_LOADER_FNS
#undef VULKAN_FN
}

// Get the function pointers from the Vulkan loader for the resolved instance API methods.
void WEAK vk_load_vulkan_instance_functions(void *user_context, VkInstance instance) {
    debug(user_context) << "    vk_load_vulkan_instance_functions (user_context: " << user_context << ")\n";
#define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn);
#define HL_USE_VULKAN_INSTANCE_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_INSTANCE_FNS
#undef VULKAN_FN
}

// Reset the instance function pointers
void WEAK vk_unload_vulkan_instance_functions(void *user_context) {
#define VULKAN_FN(fn) fn = (PFN_##fn)(nullptr);
#define HL_USE_VULKAN_INSTANCE_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_INSTANCE_FNS
#undef VULKAN_FN
}

// Get the function pointers from the Vulkan instance for the resolved driver API methods.
void WEAK vk_load_vulkan_device_functions(void *user_context, VkDevice device) {
    debug(user_context) << "    vk_load_vulkan_device_functions (user_context: " << user_context << ")\n";
#define VULKAN_FN(fn) fn = (PFN_##fn)vkGetDeviceProcAddr(device, #fn);
#define HL_USE_VULKAN_DEVICE_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_DEVICE_FNS
#undef VULKAN_FN
}

// Reset the device function pointers
void WEAK vk_unload_vulkan_device_functions(void *user_context) {
#define VULKAN_FN(fn) fn = (PFN_##fn)(nullptr);
#define HL_USE_VULKAN_DEVICE_FNS
#include "vulkan_functions.h"
#undef HL_USE_VULKAN_DEVICE_FNS
#undef VULKAN_FN
}

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERFACE_H

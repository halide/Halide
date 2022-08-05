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

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Vulkan {

// --------------------------------------------------------------------------

// Halide device interface struct for runtime specific funtion table
extern WEAK halide_device_interface_t vulkan_device_interface;

// --------------------------------------------------------------------------

// clang-format off
#define VULKAN_FN(fn) WEAK PFN_##fn fn; 
#include "vulkan_functions.h"
#undef VULKAN_FN
// clang-format on

void WEAK vk_load_vulkan_functions(VkInstance instance) {
#define VULKAN_FN(fn) fn = (PFN_##fn)vkGetInstanceProcAddr(instance, #fn);
#include "vulkan_functions.h"
#undef VULKAN_FN
}

// --

// --------------------------------------------------------------------------

}  // namespace Vulkan
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_VULKAN_INTERFACE_H

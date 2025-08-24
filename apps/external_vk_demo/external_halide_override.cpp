#include "external_halide_override.h"

#include "HalideRuntimeVulkan.h"
#include "vk_buffer_wrap_halide_defs.h"

#include <atomic>
#include <iostream>

// Global state to track the external context and memory allocator
static struct {
  std::atomic<bool> initialized{false};
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;

  // Memory allocator management
  struct halide_vulkan_memory_allocator* allocator = nullptr;
  std::atomic<bool> allocator_saved{false};

  // Global spin lock for thread safety (simple atomic flag)
  std::atomic_flag allocator_lock = ATOMIC_FLAG_INIT;
} g_external_context;

extern "C" {

int halide_vulkan_acquire_context(
    void* user_context, struct halide_vulkan_memory_allocator** allocator,
    VkInstance* instance, VkDevice* device, VkPhysicalDevice* physical_device,
    VkQueue* queue, uint32_t* queue_family_index,
    VkDebugUtilsMessengerEXT* messenger, bool create) {

  std::cout << "halide_vulkan_acquire_context called (create=" << create
            << ")\n";

  if (!g_external_context.initialized.load()) {
    return 0;
  }

  // Acquire global spin lock
  while (g_external_context.allocator_lock.test_and_set(
      std::memory_order_acquire)) {
    // Spin wait
  }

  // Provide application's Vulkan context to Halide
  *instance = g_external_context.instance;
  *device = g_external_context.device;
  *physical_device = g_external_context.physical_device;
  *queue = g_external_context.queue;
  *queue_family_index = g_external_context.queue_family_index;
  *messenger = VK_NULL_HANDLE;

  // Use saved allocator if we have one, otherwise let Halide create one
  if (g_external_context.allocator_saved.load() &&
      g_external_context.allocator != nullptr) {
    *allocator = g_external_context.allocator;
    std::cout << "Using saved Halide memory allocator\n";
  } else {
    *allocator = nullptr;
    std::cout << "Letting Halide create new memory allocator\n";
  }

  // Release global spin lock
  g_external_context.allocator_lock.clear(std::memory_order_release);

  std::cout << "Provided external Vulkan context to Halide\n";
  return 0;
}

int halide_vulkan_release_context(void* user_context, VkInstance instance,
                                  VkDevice device, VkQueue queue,
                                  VkDebugUtilsMessengerEXT messenger) {

  std::cout << "halide_vulkan_release_context called\n";
  // Application retains ownership of context - nothing to release
  return 0;
}

int halide_vulkan_export_memory_allocator(
    void* user_context, struct halide_vulkan_memory_allocator* allocator) {
  std::cout << "halide_vulkan_export_memory_allocator called\n";

  if (allocator == nullptr) {
    std::cerr << "Error: Received null allocator in export_memory_allocator\n";
    return -1;
  }

  // Acquire global spin lock
  while (g_external_context.allocator_lock.test_and_set(
      std::memory_order_acquire)) {
    // Spin wait
  }

  // Save the allocator for future acquire_context calls
  g_external_context.allocator = allocator;
  g_external_context.allocator_saved.store(true);

  // Release global spin lock
  g_external_context.allocator_lock.clear(std::memory_order_release);

  std::cout << "Successfully saved Halide memory allocator for reuse\n";
  return 0;
}

void register_external_vulkan_context(VkInstance instance, VkDevice device,
                                      VkPhysicalDevice physical_device,
                                      VkQueue queue,
                                      uint32_t queue_family_index) {

  g_external_context.instance = instance;
  g_external_context.device = device;
  g_external_context.physical_device = physical_device;
  g_external_context.queue = queue;
  g_external_context.queue_family_index = queue_family_index;
  g_external_context.initialized = true;

  std::cout << "Registered external Vulkan context with Halide\n";
}

void unregister_external_vulkan_context() {
  std::cout << "Unregistering external Vulkan context from Halide...\n";

  // Acquire global spin lock
  while (g_external_context.allocator_lock.test_and_set(
      std::memory_order_acquire)) {
    // Spin wait
  }

  // Release the memory allocator if we have one
  if (g_external_context.allocator_saved.load() &&
      g_external_context.allocator != nullptr) {
    std::cout << "Releasing Halide memory allocator...\n";
    int result = halide_vulkan_memory_allocator_release(
        nullptr, g_external_context.allocator, g_external_context.instance,
        VK_NULL_HANDLE);
    if (result != 0) {
      std::cerr << "Warning: Failed to release memory allocator, error code: "
                << result << "\n";
    } else {
      std::cout << "Successfully released Halide memory allocator\n";
    }

    g_external_context.allocator = nullptr;
    g_external_context.allocator_saved.store(false);
  }

  // Clear context
  g_external_context.initialized.store(false);
  g_external_context.instance = VK_NULL_HANDLE;
  g_external_context.device = VK_NULL_HANDLE;
  g_external_context.physical_device = VK_NULL_HANDLE;
  g_external_context.queue = VK_NULL_HANDLE;
  g_external_context.queue_family_index = 0;

  // Release global spin lock
  g_external_context.allocator_lock.clear(std::memory_order_release);

  std::cout << "Unregistered external Vulkan context from Halide\n";
}

int halide_vulkan_detach_vk_buffer(void* user_context, halide_buffer_t* buf) {
  if (buf->device == 0) {
    return halide_error_code_success;
  }
  if (buf->device_interface != halide_vulkan_device_interface()) {
    printf(
        "Error: detach called on buffer with incompatible device interface: %p "
        "vs %p\n",
        buf->device_interface, halide_vulkan_device_interface());
    return halide_error_code_incompatible_device_interface;
  }
  auto* region = reinterpret_cast<ExternalVulkanBuffer*>(buf->device);
  region->is_owner = false;
  region->handle = nullptr;
  return halide_error_code_success;
}

}  // extern "C"

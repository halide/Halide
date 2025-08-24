#pragma once

#include "HalideBuffer.h"
#include "vk_buffer_wrap_halide_defs.h"

#include <vulkan/vulkan.h>

#include <cstddef>

// Application Vulkan context
struct AppVulkanContext {
  bool initialized = false;
  VkInstance instance = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family_index = 0;
  VkDeviceMemory input_memory = VK_NULL_HANDLE;
  VkDeviceMemory output_memory = VK_NULL_HANDLE;
  void* input_mapped_memory = nullptr;
  void* output_mapped_memory = nullptr;
};

// Application buffer resources
struct AppVulkanBuffers {
  VkBuffer input_buffer = VK_NULL_HANDLE;
  VkBuffer output_buffer = VK_NULL_HANDLE;
  ExternalVulkanBuffer* input_region = nullptr;   // Heap allocated
  ExternalVulkanBuffer* output_region = nullptr;  // Heap allocated

  // Stride information calculated from Vulkan alignment requirements
  int input_stride = 0;
  int output_stride = 0;

  // Track wrapped Halide buffers for proper cleanup
  halide_buffer_t* wrapped_input_buffer = nullptr;
  halide_buffer_t* wrapped_output_buffer = nullptr;
};

// Vulkan application interface
bool initializeVulkanContext();
bool createVulkanBuffers(size_t buffer_size);
void cleanupVulkan();

// Image loading functions
Halide::Runtime::Buffer<uint8_t, 3> loadTestImage();

// VkBuffer allocation functions
bool allocateVkBuffersForImage(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image);

// VkBuffer wrapping with Halide functions
Halide::Runtime::Buffer<uint8_t, 3> wrapVkBufferInput(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image);
Halide::Runtime::Buffer<uint8_t, 2> wrapVkBufferOutput(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image);

// Data copying functions
bool copyHostDataToVkBuffer(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image,
    const Halide::Runtime::Buffer<uint8_t, 3>& vk_buffer);

// Conversion functions
bool executeConversionWithWrappedBuffers(
    const Halide::Runtime::Buffer<uint8_t, 3>& vk_input,
    const Halide::Runtime::Buffer<uint8_t, 2>& vk_output);

// Test functions
bool testJITWithExternalResources();
bool testAOTWithExternalResources();

// Access to global context (for external override registration)
AppVulkanContext& getAppVulkanContext();
AppVulkanBuffers& getAppVulkanBuffers();
#include "vulkan_app.h"

#include <iostream>

int main() {
  std::cout << "External Vulkan Demo\n";

  // Test synthetic image generation first
  auto img = loadTestImage();

  std::cout << "Image loaded successfully!\n";
  std::cout << "Dimensions: " << img.width() << "x" << img.height() << "x"
            << img.channels() << std::endl;

  // Check some pixel values to verify the image is properly loaded
  std::cout << "Sample pixel values at (10,10): ";
  std::cout << "R=" << static_cast<int>(img(10, 10, 0)) << " ";
  std::cout << "G=" << static_cast<int>(img(10, 10, 1)) << " ";
  std::cout << "B=" << static_cast<int>(img(10, 10, 2)) << std::endl;

  // Try to initialize Vulkan context
  std::cout << "\nTesting Vulkan context initialization...\n";
  if (!initializeVulkanContext()) {
    std::cout << "Vulkan not available - skipping VkBuffer allocation test\n";
    std::cout << "Image loading test passed!\n";
    return 0;
  }

  // Test VkBuffer allocation
  std::cout << "Testing VkBuffer allocation...\n";
  if (!allocateVkBuffersForImage(img)) {
    std::cerr << "Failed to allocate VkBuffers\n";
    cleanupVulkan();
    return -1;
  }

  // Test VkBuffer wrapping with Halide
  std::cout << "\nTesting VkBuffer wrapping with Halide...\n";
  auto vk_input = wrapVkBufferInput(img);
  if (!vk_input.raw_buffer() || !vk_input.raw_buffer()->device_interface) {
    std::cerr << "Failed to wrap input VkBuffer\n";
    cleanupVulkan();
    return -1;
  }

  auto vk_output = wrapVkBufferOutput(img);
  if (!vk_output.raw_buffer() || !vk_output.raw_buffer()->device_interface) {
    std::cerr << "Failed to wrap output VkBuffer\n";
    cleanupVulkan();
    return -1;
  }

  std::cout << "Successfully created wrapped Halide buffers:\n";
  std::cout << "  Input: " << vk_input.width() << "x" << vk_input.height()
            << "x" << vk_input.channels() << "\n";
  std::cout << "  Output: " << vk_output.width() << "x" << vk_output.height()
            << "\n";

  // Copy host image data to wrapped VkBuffer input
  std::cout << "\nCopying host image data to VkBuffer...\n";
  if (!copyHostDataToVkBuffer(img, vk_input)) {
    std::cerr << "Failed to copy host data to VkBuffer\n";
    cleanupVulkan();
    return -1;
  }
  std::cout << "Successfully copied host image data to VkBuffer!\n";

  // Execute RGB to grayscale conversion using wrapped buffers
  std::cout << "\nExecuting RGB to grayscale conversion...\n";
  if (!executeConversionWithWrappedBuffers(vk_input, vk_output)) {
    std::cerr << "Failed to execute conversion with wrapped buffers\n";
    cleanupVulkan();
    return -1;
  }
  std::cout << "Successfully executed RGB to grayscale conversion!\n";

  std::cout << "\nAll tests passed!\n";

  // Cleanup
  cleanupVulkan();
  std::cout << "Cleaned up Vulkan resources\n";
  return 0;
}
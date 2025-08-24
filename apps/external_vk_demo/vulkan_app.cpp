#include "vulkan_app.h"

#include "Halide.h"
#include "HalideRuntimeVulkan.h"
#include "convert_generator.h"  // AOT generated header
#include "external_halide_override.h"

#include <iostream>
#include <string>
#include <vector>

// Global state for the demo
static AppVulkanContext g_app_context{};
static AppVulkanBuffers g_app_buffers{};

bool initializeVulkanContext() {
  // First, check if we can get the Vulkan loader version
  std::cout << "Checking Vulkan availability...\n";

  // Check available instance extensions
  uint32_t extension_count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);

  std::vector<VkExtensionProperties> available_extensions(extension_count);
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count,
                                         available_extensions.data());

  std::cout << "Available Vulkan extensions: " << extension_count << std::endl;
  for (const auto& extension : available_extensions) {
    std::cout << "  " << extension.extensionName << std::endl;
  }

  // Check instance version
  uint32_t api_version = 0;
  VkResult version_result = vkEnumerateInstanceVersion(&api_version);
  if (version_result == VK_SUCCESS) {
    std::cout << "Vulkan API version: " << VK_VERSION_MAJOR(api_version) << "."
              << VK_VERSION_MINOR(api_version) << "."
              << VK_VERSION_PATCH(api_version) << std::endl;
  }

  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Halide External Context Demo";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  // Enable required extensions for macOS (MoltenVK)
  std::vector<const char*> required_extensions;

  // Check for portability enumeration extension (required on macOS)
  bool has_portability = false;
  for (const auto& extension : available_extensions) {
    if (strcmp(extension.extensionName,
               VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
      has_portability = true;
      break;
    }
  }

  if (has_portability) {
    required_extensions.push_back(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    std::cout << "Enabling portability enumeration extension for macOS\n";
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount =
      static_cast<uint32_t>(required_extensions.size());
  create_info.ppEnabledExtensionNames = required_extensions.data();
  create_info.enabledLayerCount = 0;
  create_info.ppEnabledLayerNames = nullptr;

  // Enable portability subset flag for macOS
  if (has_portability) {
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  std::cout << "Creating Vulkan instance...\n";
  VkResult result =
      vkCreateInstance(&create_info, nullptr, &g_app_context.instance);
  if (result != VK_SUCCESS) {
    std::cerr << "Failed to create Vulkan instance, error code: " << result;
    switch (result) {
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        std::cerr << " (VK_ERROR_OUT_OF_HOST_MEMORY)\n";
        break;
      case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        std::cerr << " (VK_ERROR_OUT_OF_DEVICE_MEMORY)\n";
        break;
      case VK_ERROR_INITIALIZATION_FAILED:
        std::cerr << " (VK_ERROR_INITIALIZATION_FAILED)\n";
        break;
      case VK_ERROR_LAYER_NOT_PRESENT:
        std::cerr << " (VK_ERROR_LAYER_NOT_PRESENT)\n";
        break;
      case VK_ERROR_EXTENSION_NOT_PRESENT:
        std::cerr << " (VK_ERROR_EXTENSION_NOT_PRESENT)\n";
        break;
      case VK_ERROR_INCOMPATIBLE_DRIVER:
        std::cerr << " (VK_ERROR_INCOMPATIBLE_DRIVER)\n";
        break;
      default:
        std::cerr << " (unknown error)\n";
        break;
    }
    return false;
  }

  // Find physical device
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(g_app_context.instance, &device_count, nullptr);
  if (device_count == 0) {
    std::cerr << "No Vulkan physical devices found\n";
    return false;
  }

  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(g_app_context.instance, &device_count,
                             devices.data());
  g_app_context.physical_device = devices[0];

  // Find queue family
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(g_app_context.physical_device,
                                           &queue_family_count, nullptr);

  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(g_app_context.physical_device,
                                           &queue_family_count,
                                           queue_families.data());

  for (uint32_t i = 0; i < queue_family_count; i++) {
    if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      g_app_context.queue_family_index = i;
      break;
    }
  }

  // Create logical device
  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = g_app_context.queue_family_index;
  queue_create_info.queueCount = 1;
  float queue_priority = 1.0f;
  queue_create_info.pQueuePriorities = &queue_priority;

  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_create_info;

  if (vkCreateDevice(g_app_context.physical_device, &device_info, nullptr,
                     &g_app_context.device) != VK_SUCCESS) {
    std::cerr << "Failed to create logical device\n";
    return false;
  }

  vkGetDeviceQueue(g_app_context.device, g_app_context.queue_family_index, 0,
                   &g_app_context.queue);

  g_app_context.initialized = true;
  std::cout << "Initialized application Vulkan context\n";
  return true;
}

void cleanupVulkan() {
  if (g_app_buffers.wrapped_input_buffer) {
    std::cout << "Clearing wrapped input buffer reference...\n";
    if (halide_vulkan_detach_vk_buffer(nullptr,
                                       g_app_buffers.wrapped_input_buffer)) {
      std::cerr << "Failed to detach wrapped input buffer\n";
    }
  }
  if (g_app_buffers.wrapped_output_buffer) {
    std::cout << "Clearing wrapped output buffer reference...\n";
    if (halide_vulkan_detach_vk_buffer(nullptr,
                                       g_app_buffers.wrapped_output_buffer)) {
      std::cerr << "Failed to detach wrapped output buffer\n";
    }
  }

  if (g_app_context.input_mapped_memory) {
    vkUnmapMemory(g_app_context.device, g_app_context.input_memory);
    g_app_context.input_mapped_memory = nullptr;
  }
  if (g_app_context.output_mapped_memory) {
    vkUnmapMemory(g_app_context.device, g_app_context.output_memory);
    g_app_context.output_mapped_memory = nullptr;
  }

  if (g_app_buffers.input_buffer) {
    vkDestroyBuffer(g_app_context.device, g_app_buffers.input_buffer, nullptr);
    g_app_buffers.input_buffer = VK_NULL_HANDLE;
  }
  if (g_app_buffers.output_buffer) {
    vkDestroyBuffer(g_app_context.device, g_app_buffers.output_buffer, nullptr);
    g_app_buffers.output_buffer = VK_NULL_HANDLE;
  }
  if (g_app_context.input_memory) {
    vkFreeMemory(g_app_context.device, g_app_context.input_memory, nullptr);
    g_app_context.input_memory = VK_NULL_HANDLE;
  }
  if (g_app_context.output_memory) {
    vkFreeMemory(g_app_context.device, g_app_context.output_memory, nullptr);
    g_app_context.output_memory = VK_NULL_HANDLE;
  }

  // Unregister external context and release memory allocator
  unregister_external_vulkan_context();

  // Free heap-allocated regions
  if (g_app_buffers.input_region) {
    delete g_app_buffers.input_region;
    g_app_buffers.input_region = nullptr;
  }
  if (g_app_buffers.output_region) {
    delete g_app_buffers.output_region;
    g_app_buffers.output_region = nullptr;
  }

  if (g_app_context.device) {
    vkDestroyDevice(g_app_context.device, nullptr);
    g_app_context.device = VK_NULL_HANDLE;
  }
  if (g_app_context.instance) {
    vkDestroyInstance(g_app_context.instance, nullptr);
    g_app_context.instance = VK_NULL_HANDLE;
  }

  g_app_context.initialized = false;
  std::cout << "Cleaned up Vulkan resources\n";
}

// Access functions for external override registration
AppVulkanContext& getAppVulkanContext() {
  return g_app_context;
}

AppVulkanBuffers& getAppVulkanBuffers() {
  return g_app_buffers;
}

Halide::Runtime::Buffer<uint8_t, 3> loadTestImage() {
  std::cout << "Creating synthetic test image for external context demo"
            << std::endl;

  // Create a simple synthetic RGB image for testing with proper interleaved
  // layout
  const int width = 256, height = 256, channels = 3;

  // Allocate buffer with proper interleaved RGB layout [x, y, c] where
  // stride(0) = 3
  Halide::Runtime::Buffer<uint8_t, 3> synthetic_img =
      Halide::Runtime::Buffer<uint8_t, 3>::make_interleaved(width, height,
                                                            channels);

  // Fill with a simple pattern - checkerboard with gradients
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      // Create a checkerboard pattern with gradients
      bool checker = ((x / 32) + (y / 32)) % 2;
      if (checker) {
        synthetic_img(x, y, 0) = (x + y) % 256;  // Red gradient
        synthetic_img(x, y, 1) = (x * 2) % 256;  // Green gradient
        synthetic_img(x, y, 2) = (y * 2) % 256;  // Blue gradient
      } else {
        synthetic_img(x, y, 0) = 255 - (x % 256);  // Inverted red
        synthetic_img(x, y, 1) = 128;              // Fixed green
        synthetic_img(x, y, 2) = 255 - (y % 256);  // Inverted blue
      }
    }
  }

  std::cout << "Created synthetic RGB test image: " << width << "x" << height
            << " pixels with interleaved layout" << std::endl;
  return synthetic_img;
}

bool allocateVkBuffersForImage(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image) {
  if (!g_app_context.initialized) {
    std::cerr << "Vulkan context not initialized\n";
    return false;
  }

  // Initial buffer sizes for VkBuffer creation (will be recalculated after
  // alignment)
  size_t initial_input_size = host_image.width() * host_image.height() *
                              host_image.channels();  // RGB: 3 channels
  size_t initial_output_size =
      host_image.width() * host_image.height() * 1;  // Grayscale: 1 channel

  std::cout << "Allocating VkBuffers for image processing:\n";
  std::cout << "  Input (RGB): " << host_image.width() << "x"
            << host_image.height() << "x" << host_image.channels()
            << " (initial: " << initial_input_size << " bytes)\n";
  std::cout << "  Output (Grayscale): " << host_image.width() << "x"
            << host_image.height() << "x1 (initial: " << initial_output_size
            << " bytes)\n";

  // Create input VkBuffer (RGB)
  VkBufferCreateInfo input_buffer_info{};
  input_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  input_buffer_info.size = initial_input_size;
  input_buffer_info.usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  input_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(g_app_context.device, &input_buffer_info, nullptr,
                     &g_app_buffers.input_buffer) != VK_SUCCESS) {
    std::cerr << "Failed to create input VkBuffer\n";
    return false;
  }

  // Create output VkBuffer (Grayscale)
  VkBufferCreateInfo output_buffer_info{};
  output_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  output_buffer_info.size = initial_output_size;
  output_buffer_info.usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  output_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(g_app_context.device, &output_buffer_info, nullptr,
                     &g_app_buffers.output_buffer) != VK_SUCCESS) {
    std::cerr << "Failed to create output VkBuffer\n";
    return false;
  }

  // Get memory requirements for both buffers
  VkMemoryRequirements input_mem_req, output_mem_req;
  vkGetBufferMemoryRequirements(g_app_context.device,
                                g_app_buffers.input_buffer, &input_mem_req);
  vkGetBufferMemoryRequirements(g_app_context.device,
                                g_app_buffers.output_buffer, &output_mem_req);

  // Calculate stride based on Vulkan alignment requirements
  // For RGB interleaved input: stride is total bytes per row with alignment
  size_t input_row_bytes = host_image.width() * host_image.channels();
  g_app_buffers.input_stride = (input_row_bytes + input_mem_req.alignment - 1) &
                               ~(input_mem_req.alignment - 1);

  // For grayscale output: stride is total bytes per row with alignment
  size_t output_row_bytes = host_image.width();
  g_app_buffers.output_stride =
      (output_row_bytes + output_mem_req.alignment - 1) &
      ~(output_mem_req.alignment - 1);

  // Recalculate actual buffer sizes based on aligned stride
  size_t input_size = g_app_buffers.input_stride * host_image.height();
  size_t output_size = g_app_buffers.output_stride * host_image.height();

  // Find suitable memory type (host-visible and coherent)
  VkPhysicalDeviceMemoryProperties mem_properties;
  vkGetPhysicalDeviceMemoryProperties(g_app_context.physical_device,
                                      &mem_properties);

  // Find memory type for input buffer
  uint32_t input_memory_type_index = UINT32_MAX;
  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
    if ((input_mem_req.memoryTypeBits & (1 << i)) &&
        (mem_properties.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      input_memory_type_index = i;
      break;
    }
  }

  // Find memory type for output buffer
  uint32_t output_memory_type_index = UINT32_MAX;
  for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
    if ((output_mem_req.memoryTypeBits & (1 << i)) &&
        (mem_properties.memoryTypes[i].propertyFlags &
         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
      output_memory_type_index = i;
      break;
    }
  }

  if (input_memory_type_index == UINT32_MAX ||
      output_memory_type_index == UINT32_MAX) {
    std::cerr << "Failed to find suitable memory types\n";
    return false;
  }

  // Allocate separate memory for input buffer
  VkMemoryAllocateInfo input_alloc_info{};
  input_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  input_alloc_info.allocationSize = input_mem_req.size;
  input_alloc_info.memoryTypeIndex = input_memory_type_index;

  if (vkAllocateMemory(g_app_context.device, &input_alloc_info, nullptr,
                       &g_app_context.input_memory) != VK_SUCCESS) {
    std::cerr << "Failed to allocate input VkBuffer memory\n";
    return false;
  }

  // Allocate separate memory for output buffer
  VkMemoryAllocateInfo output_alloc_info{};
  output_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  output_alloc_info.allocationSize = output_mem_req.size;
  output_alloc_info.memoryTypeIndex = output_memory_type_index;

  if (vkAllocateMemory(g_app_context.device, &output_alloc_info, nullptr,
                       &g_app_context.output_memory) != VK_SUCCESS) {
    std::cerr << "Failed to allocate output VkBuffer memory\n";
    return false;
  }

  // Bind buffers to their separate memory (both at offset 0)
  if (vkBindBufferMemory(g_app_context.device, g_app_buffers.input_buffer,
                         g_app_context.input_memory, 0) != VK_SUCCESS) {
    std::cerr << "Failed to bind input VkBuffer memory\n";
    return false;
  }

  if (vkBindBufferMemory(g_app_context.device, g_app_buffers.output_buffer,
                         g_app_context.output_memory, 0) != VK_SUCCESS) {
    std::cerr << "Failed to bind output VkBuffer memory\n";
    return false;
  }

  // Map memory for CPU access (separate mappings)
  if (vkMapMemory(g_app_context.device, g_app_context.input_memory, 0,
                  VK_WHOLE_SIZE, 0,
                  &g_app_context.input_mapped_memory) != VK_SUCCESS) {
    std::cerr << "Failed to map input VkBuffer memory\n";
    return false;
  }

  if (vkMapMemory(g_app_context.device, g_app_context.output_memory, 0,
                  VK_WHOLE_SIZE, 0,
                  &g_app_context.output_mapped_memory) != VK_SUCCESS) {
    std::cerr << "Failed to map output VkBuffer memory\n";
    return false;
  }

  // Allocate ExternalVulkanBuffer regions on heap
  g_app_buffers.input_region = new ExternalVulkanBuffer();
  g_app_buffers.input_region->handle = &g_app_buffers.input_buffer;
  g_app_buffers.input_region->offset = 0;
  g_app_buffers.input_region->size = input_size;
  g_app_buffers.input_region->is_owner = true;

  g_app_buffers.output_region = new ExternalVulkanBuffer();
  g_app_buffers.output_region->handle = &g_app_buffers.output_buffer;
  g_app_buffers.output_region->offset = 0;
  g_app_buffers.output_region->size = output_size;
  g_app_buffers.output_region->is_owner = true;

  std::cout << "Successfully allocated and bound VkBuffers:\n";
  std::cout << "  Input buffer: separate memory, size " << input_size
            << " bytes\n";
  std::cout << "  Output buffer: separate memory, size " << output_size
            << " bytes\n";
  std::cout << "  Input stride: " << g_app_buffers.input_stride
            << " bytes per row\n";
  std::cout << "  Output stride: " << g_app_buffers.output_stride
            << " bytes per row\n";

  return true;
}

// Helper function to set up proper Halide buffer dimensions with stride
void setupHalideBufferDimensions(halide_buffer_t* buf, int width, int height,
                                 int channels, int stride_bytes) {
  if (channels > 1) {
    // RGB interleaved: [x, y, c]
    buf->dimensions = 3;
    buf->dim[0].min = 0;
    buf->dim[0].extent = width;
    buf->dim[0].stride = channels;  // Skip channels to get to next x

    buf->dim[1].min = 0;
    buf->dim[1].extent = height;
    buf->dim[1].stride =
        stride_bytes;  // Total bytes per row (includes alignment)

    buf->dim[2].min = 0;
    buf->dim[2].extent = channels;
    buf->dim[2].stride = 1;  // Adjacent channels
  } else {
    // Grayscale: [x, y]
    buf->dimensions = 2;
    buf->dim[0].min = 0;
    buf->dim[0].extent = width;
    buf->dim[0].stride = 1;

    buf->dim[1].min = 0;
    buf->dim[1].extent = height;
    buf->dim[1].stride =
        stride_bytes;  // Total bytes per row (includes alignment)
  }
}

Halide::Runtime::Buffer<uint8_t, 3> wrapVkBufferInput(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image) {
  if (!g_app_context.initialized || !g_app_buffers.input_buffer) {
    std::cerr
        << "VkBuffer not allocated - call allocateVkBuffersForImage first\n";
    return Halide::Runtime::Buffer<uint8_t, 3>();
  }

  std::cout << "Wrapping input VkBuffer with Halide buffer...\n";

  // Create a Halide buffer with the same dimensions as the host image
  // (GPU-only)
  Halide::Runtime::Buffer<uint8_t, 3> vk_input_buffer(
      nullptr, host_image.width(), host_image.height(), host_image.channels());

  // Use the actual stride calculated from Vulkan alignment requirements
  int stride = g_app_buffers.input_stride;
  setupHalideBufferDimensions(vk_input_buffer.raw_buffer(), host_image.width(),
                              host_image.height(), host_image.channels(),
                              stride);

  // Register our external context with Halide
  register_external_vulkan_context(g_app_context.instance, g_app_context.device,
                                   g_app_context.physical_device,
                                   g_app_context.queue,
                                   g_app_context.queue_family_index);

  // Wrap the VkBuffer using the MemoryRegion pattern
  uint64_t memory_region_ptr =
      reinterpret_cast<uint64_t>(g_app_buffers.input_region);

  // Ensure device interface is set before wrapping
  vk_input_buffer.raw_buffer()->device_interface =
      halide_vulkan_device_interface();
  g_app_buffers.wrapped_input_buffer = vk_input_buffer.raw_buffer();

  int result = halide_vulkan_wrap_vk_buffer(
      nullptr, g_app_buffers.wrapped_input_buffer, memory_region_ptr);
  if (result != 0) {
    std::cerr << "Failed to wrap input VkBuffer with Halide, error code: "
              << result << "\n";
    return Halide::Runtime::Buffer<uint8_t, 3>();
  }

  // Verify buffer setup
  size_t calculated_size = vk_input_buffer.size_in_bytes();
  std::cout << "Successfully wrapped input VkBuffer (" << host_image.width()
            << "x" << host_image.height() << "x" << host_image.channels()
            << "), calculated size: " << calculated_size << " bytes\n";

  return vk_input_buffer;
}

Halide::Runtime::Buffer<uint8_t, 2> wrapVkBufferOutput(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image) {
  if (!g_app_context.initialized || !g_app_buffers.output_buffer) {
    std::cerr
        << "VkBuffer not allocated - call allocateVkBuffersForImage first\n";
    return Halide::Runtime::Buffer<uint8_t, 2>();
  }

  std::cout << "Wrapping output VkBuffer with Halide buffer...\n";

  // Create a Halide buffer for grayscale output (2D - width x height, no
  // channels, GPU-only)
  Halide::Runtime::Buffer<uint8_t, 2> vk_output_buffer(
      nullptr, host_image.width(), host_image.height());

  // Use the actual stride calculated from Vulkan alignment requirements
  int stride = g_app_buffers.output_stride;
  setupHalideBufferDimensions(vk_output_buffer.raw_buffer(), host_image.width(),
                              host_image.height(), 1, stride);

  // Wrap the VkBuffer using the MemoryRegion pattern
  uint64_t memory_region_ptr =
      reinterpret_cast<uint64_t>(g_app_buffers.output_region);

  // Ensure device interface is set before wrapping
  vk_output_buffer.raw_buffer()->device_interface =
      halide_vulkan_device_interface();
  g_app_buffers.wrapped_output_buffer = vk_output_buffer.raw_buffer();

  int result = halide_vulkan_wrap_vk_buffer(
      nullptr, g_app_buffers.wrapped_output_buffer, memory_region_ptr);
  if (result != 0) {
    std::cerr << "Failed to wrap output VkBuffer with Halide, error code: "
              << result << "\n";
    return Halide::Runtime::Buffer<uint8_t, 2>();
  }

  // Verify buffer setup
  size_t calculated_size = vk_output_buffer.size_in_bytes();
  std::cout << "Successfully wrapped output VkBuffer (" << host_image.width()
            << "x" << host_image.height()
            << "), calculated size: " << calculated_size << " bytes\n";

  return vk_output_buffer;
}

bool copyHostDataToVkBuffer(
    const Halide::Runtime::Buffer<uint8_t, 3>& host_image,
    const Halide::Runtime::Buffer<uint8_t, 3>& vk_buffer) {
  if (!g_app_context.initialized || !g_app_buffers.input_buffer) {
    std::cerr
        << "Vulkan context not initialized or input buffer not allocated\n";
    return false;
  }

  std::cout
      << "Copying host image data to VkBuffer using halide_buffer_copy...\n";
  std::cout << "  Source (host): " << host_image.width() << "x"
            << host_image.height() << "x" << host_image.channels() << "\n";
  std::cout << "  Dest (VkBuffer): " << vk_buffer.width() << "x"
            << vk_buffer.height() << "x" << vk_buffer.channels() << "\n";

  // Use Halide's buffer copy function to handle the transfer
  // Need to cast away const for halide_buffer_copy API
  int result = halide_buffer_copy(
      nullptr, const_cast<halide_buffer_t*>(host_image.raw_buffer()),
      halide_vulkan_device_interface(),
      const_cast<halide_buffer_t*>(vk_buffer.raw_buffer()));

  if (result != 0) {
    std::cerr << "halide_buffer_copy failed with error code: " << result
              << "\n";
    return false;
  }

  std::cout << "Successfully copied host image data to VkBuffer!\n";
  return true;
}

bool executeConversionWithWrappedBuffers(
    const Halide::Runtime::Buffer<uint8_t, 3>& vk_input,
    const Halide::Runtime::Buffer<uint8_t, 2>& vk_output) {
  if (!g_app_context.initialized) {
    std::cerr << "Vulkan context not initialized\n";
    return false;
  }

  std::cout << "Executing RGB to grayscale conversion using AOT generated "
               "function...\n";
  std::cout << "  Input buffer: " << vk_input.width() << "x"
            << vk_input.height() << "x" << vk_input.channels() << "\n";
  std::cout << "  Output buffer: " << vk_output.width() << "x"
            << vk_output.height() << "\n";

  // Call the AOT generated convert_generator function
  int result =
      convert_generator(const_cast<halide_buffer_t*>(vk_input.raw_buffer()),
                        const_cast<halide_buffer_t*>(vk_output.raw_buffer()));

  if (result != 0) {
    std::cerr << "convert_generator failed with error code: " << result << "\n";
    return false;
  }

  std::cout << "Successfully executed RGB to grayscale conversion!\n";
  return true;
}
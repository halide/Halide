#pragma once

#include <cstddef>

// External Vulkan buffer management structures (duplicated from Halide
// internal)
enum class ExternalMemoryVisibility {
  InvalidVisibility,  //< invalid enum value
  HostOnly,           //< host local
  DeviceOnly,         //< device local
  DeviceToHost,       //< transfer from device to host
  HostToDevice,       //< transfer from host to device
  DefaultVisibility,  //< default visibility
};

enum class ExternalMemoryUsage {
  InvalidUsage,    //< invalid enum value
  StaticStorage,   //< intended for static storage
  DynamicStorage,  //< intended for dynamic storage
  UniformStorage,  //< intended for uniform storage
  TransferSrc,     //< intended for staging storage updates (source)
  TransferDst,     //< intended for staging storage updates (destination)
  TransferSrcDst,  //< intended for staging storage updates (source or
                   //destination)
  DefaultUsage     //< default usage
};

enum class ExternalMemoryCaching {
  InvalidCaching,    //< invalid enum value
  Cached,            //< cached
  Uncached,          //< uncached
  CachedCoherent,    //< cached and coherent
  UncachedCoherent,  //< uncached but still coherent
  DefaultCaching     //< default caching
};

struct ExternalMemoryProperties {
  ExternalMemoryVisibility visibility =
      ExternalMemoryVisibility::InvalidVisibility;
  ExternalMemoryUsage usage = ExternalMemoryUsage::InvalidUsage;
  ExternalMemoryCaching caching = ExternalMemoryCaching::InvalidCaching;
  size_t alignment = 0;  //< required alignment of allocations
  size_t nearest_multiple =
      0;  //< require the allocation size to round up to nearest multiple
};

struct ExternalMemoryRange {
  size_t head_offset = 0;  //< byte offset from start of region
  size_t tail_offset = 0;  //< byte offset from end of region
};

struct ExternalVulkanBuffer {
  void* handle = nullptr;     //< client data storing native handle (VkBuffer*)
  size_t offset = 0;          //< offset from base address in block (in bytes)
  size_t size = 0;            //< allocated size (in bytes)
  ExternalMemoryRange range;  //< optional range (e.g. for handling crops, etc)
  bool dedicated =
      false;  //< flag indicating whether allocation is one dedicated resource
  bool is_owner =
      true;  //< flag indicating whether allocation is owned by this region
  ExternalMemoryProperties properties;  //< properties for the allocated region
};
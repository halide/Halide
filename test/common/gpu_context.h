#if defined(TEST_OPENCL)
// Implement OpenCL custom context.

#define CL_TARGET_OPENCL_VERSION 120
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

// Create the global context. This is just a helper function not called by Halide.
inline bool create_opencl_context(cl_context &cl_ctx, cl_command_queue &cl_q) {
    cl_int err = 0;

    const cl_uint maxPlatforms = 4;
    cl_platform_id platforms[maxPlatforms];
    cl_uint platformCount = 0;

    err = clGetPlatformIDs(maxPlatforms, platforms, &platformCount);
    if (err != CL_SUCCESS) {
        printf("clGetPlatformIDs failed (%d)\n", err);
        return false;
    }

    cl_platform_id platform = nullptr;

    if (platformCount > 0) {
        platform = platforms[0];
    }
    if (platform == nullptr) {
        printf("Failed to get platform\n");
        return false;
    }

    cl_device_type device_type = CL_DEVICE_TYPE_ALL;

    // Make sure we have a device
    const cl_uint maxDevices = 4;
    cl_device_id devices[maxDevices];
    cl_uint deviceCount = 0;
    err = clGetDeviceIDs(platform, device_type, maxDevices, devices, &deviceCount);
    if (err != CL_SUCCESS) {
        printf("clGetDeviceIDs failed (%d)\n", err);
        return false;
    }
    if (deviceCount == 0) {
        printf("Failed to get device\n");
        return false;
    }

    cl_device_id dev = devices[deviceCount - 1];

    // Create context and command queue.
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform,
                                          0};
    cl_ctx = clCreateContext(properties, 1, &dev, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateContext failed (%d)\n", err);
        return false;
    }

    cl_q = clCreateCommandQueue(cl_ctx, dev, 0, &err);
    if (err != CL_SUCCESS) {
        printf("clCreateCommandQueue failed (%d)\n", err);
        return false;
    }
    return true;
}

inline void destroy_opencl_context(cl_context cl_ctx, cl_command_queue cl_q) {
    clReleaseCommandQueue(cl_q);
    clReleaseContext(cl_ctx);
}

#elif defined(TEST_CUDA)
// Implement CUDA custom context.
#include <cuda.h>

inline bool create_cuda_context(CUcontext &cuda_ctx) {
    // Initialize CUDA
    CUresult err = cuInit(0);
    if (err != CUDA_SUCCESS) {
        printf("cuInit failed (%d)\n", err);
        return false;
    }

    // Make sure we have a device
    int deviceCount = 0;
    err = cuDeviceGetCount(&deviceCount);
    if (err != CUDA_SUCCESS) {
        printf("cuGetDeviceCount failed (%d)\n", err);
        return false;
    }
    if (deviceCount <= 0) {
        printf("No CUDA devices available\n");
        return false;
    }

    CUdevice dev;
    // Get device
    CUresult status;
    // Try to get a device >0 first, since 0 should be our display device
    // For now, don't try devices > 2 to maintain compatibility with previous behavior.
    if (deviceCount > 2) deviceCount = 2;
    for (int id = deviceCount - 1; id >= 0; id--) {
        status = cuDeviceGet(&dev, id);
        if (status == CUDA_SUCCESS) break;
    }

    if (status != CUDA_SUCCESS) {
        printf("Failed to get CUDA device\n");
        return status;
    }

    // Create context
    err = cuCtxCreate(&cuda_ctx, 0, dev);
    if (err != CUDA_SUCCESS) {
        printf("cuCtxCreate failed (%d)\n", err);
        return false;
    }

    return true;
}

inline void destroy_cuda_context(CUcontext cuda_ctx) {
    cuCtxDestroy(cuda_ctx);
}

#elif defined(TEST_METAL) && defined(__OBJC__)
#include <Metal/MTLCommandQueue.h>
#include <Metal/MTLDevice.h>

inline bool create_metal_context(id<MTLDevice> &device, id<MTLCommandQueue> &queue) {
    device = MTLCreateSystemDefaultDevice();
    if (device == nullptr) {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        if (devices != nullptr) {
            device = devices[0];
        }
    }
    if (device == nullptr) {
        printf("Failed to find Metal device.\n");
        return false;
    }
    queue = [device newCommandQueue];
    if (queue == nullptr) {
        printf("Failed to create Metal command queue.\n");
        return false;
    }
    return true;
}

inline void destroy_metal_context(id<MTLDevice> device, id<MTLCommandQueue> queue) {
    [queue release];
    [device release];
}

#elif defined(TEST_WEBGPU)

#if defined(__EMSCRIPTEN__)
#include <webgpu/webgpu_cpp.h>
#else
#include "mini_webgpu.h"
#endif

#include <cstdint>

inline bool create_webgpu_context(WGPUInstance *instance_out, WGPUAdapter *adapter_out, WGPUDevice *device_out, WGPUBuffer *staging_buffer_out) {
    struct Results {
        WGPUInstance instance = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUBuffer staging_buffer = nullptr;
        bool success = true;
    } results;

    // Check if TimedWaitAny feature is available before requesting it.
    WGPUBool has_timed_wait = wgpuHasInstanceFeature(WGPUInstanceFeatureName_TimedWaitAny);

    // Create instance with TimedWaitAny feature enabled so we can use
    // wgpuInstanceWaitAny with non-zero timeouts.
    WGPUInstanceFeatureName required_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor instance_desc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    if (has_timed_wait) {
        instance_desc.requiredFeatureCount = 1;
        instance_desc.requiredFeatures = required_features;
    }
    results.instance = wgpuCreateInstance(&instance_desc);

    // Request adapter and wait on the future (same API as webgpu.cpp).
    auto request_adapter_callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void *userdata1, void *userdata2) {
        (void)message;
        (void)userdata2;
        auto *r = (Results *)userdata1;
        if (status != WGPURequestAdapterStatus_Success) {
            r->success = false;
            return;
        }
        r->adapter = adapter;
    };

    WGPURequestAdapterCallbackInfo adapter_callback_info = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapter_callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    adapter_callback_info.callback = request_adapter_callback;
    adapter_callback_info.userdata1 = &results;

    WGPUFuture adapter_future = wgpuInstanceRequestAdapter(results.instance, nullptr, adapter_callback_info);

    WGPUFutureWaitInfo adapter_wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
    adapter_wait_info.future = adapter_future;
    wgpuInstanceWaitAny(results.instance, 1, &adapter_wait_info, UINT64_MAX);

    if (!results.success || results.adapter == nullptr) {
        *instance_out = results.instance;
        *adapter_out = nullptr;
        *device_out = nullptr;
        *staging_buffer_out = nullptr;
        return false;
    }

    // Build device descriptor (limits from adapter when supported).
    WGPULimits requestedLimits = WGPU_LIMITS_INIT;
#ifdef WITH_DAWN_NATIVE
    WGPULimits supportedLimits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(results.adapter, &supportedLimits) == WGPUStatus_Success) {
        requestedLimits.maxBufferSize = supportedLimits.maxBufferSize;
        requestedLimits.maxStorageBufferBindingSize = supportedLimits.maxStorageBufferBindingSize;
        requestedLimits.maxComputeWorkgroupStorageSize = supportedLimits.maxComputeWorkgroupStorageSize;
    }
#endif

    auto device_lost_callback = [](WGPUDevice const *device, WGPUDeviceLostReason reason,
                                   WGPUStringView message,
                                   void *userdata1, void *userdata2) {
        (void)device;
        (void)userdata1;
        (void)userdata2;
        if (reason == WGPUDeviceLostReason_Destroyed) {
            return;
        }
        fprintf(stderr, "WGPU Device Lost: %d %.*s\n", (int)reason, (int)message.length, message.data);
        abort();
    };

    WGPUDeviceDescriptor device_desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    device_desc.requiredLimits = &requestedLimits;
    device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    device_desc.deviceLostCallbackInfo.callback = device_lost_callback;
    device_desc.deviceLostCallbackInfo.userdata1 = nullptr;
    device_desc.deviceLostCallbackInfo.userdata2 = nullptr;

    // Request device and wait on the future (same API as webgpu.cpp).
    auto request_device_callback = [](WGPURequestDeviceStatus status,
                                      WGPUDevice device,
                                      WGPUStringView message,
                                      void *userdata1, void *userdata2) {
        (void)message;
        (void)userdata2;
        auto *r = (Results *)userdata1;
        if (status != WGPURequestDeviceStatus_Success) {
            r->success = false;
            return;
        }
        r->device = device;

        constexpr int kStagingBufferSize = 4 * 1024 * 1024;
        WGPUBufferDescriptor buffer_desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        buffer_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        buffer_desc.size = kStagingBufferSize;
        r->staging_buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
        if (r->staging_buffer == nullptr) {
            r->success = false;
        }
    };

    WGPURequestDeviceCallbackInfo device_callback_info = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    device_callback_info.mode = WGPUCallbackMode_WaitAnyOnly;
    device_callback_info.callback = request_device_callback;
    device_callback_info.userdata1 = &results;

    WGPUFuture device_future = wgpuAdapterRequestDevice(results.adapter, &device_desc, device_callback_info);

    WGPUFutureWaitInfo device_wait_info = WGPU_FUTURE_WAIT_INFO_INIT;
    device_wait_info.future = device_future;
    wgpuInstanceWaitAny(results.instance, 1, &device_wait_info, UINT64_MAX);

    if (!results.success || results.device == nullptr) {
        *instance_out = results.instance;
        *adapter_out = results.adapter;
        *device_out = nullptr;
        *staging_buffer_out = nullptr;
        return false;
    }

    *instance_out = results.instance;
    *adapter_out = results.adapter;
    *device_out = results.device;
    *staging_buffer_out = results.staging_buffer;
    return true;
}

inline void destroy_webgpu_context(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUBuffer staging_buffer) {
    wgpuBufferRelease(staging_buffer);
    if (device) {
        wgpuDeviceDestroy(device);  // Required for Dawn native to allow process to exit cleanly.
        wgpuDeviceRelease(device);
    }
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
}

#endif

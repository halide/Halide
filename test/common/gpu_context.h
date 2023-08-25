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

extern "C" {
// TODO: Remove all of this when wgpuInstanceProcessEvents() is supported.
// See https://github.com/halide/Halide/issues/7248
#ifdef WITH_DAWN_NATIVE
// From <unistd.h>, used to spin-lock while waiting for device initialization.
int usleep(uint32_t);
#else
// Defined by Emscripten, and used to yield execution to asynchronous Javascript
// work in combination with Emscripten's "Asyncify" mechanism.
void emscripten_sleep(unsigned int ms);
#endif
}

inline bool create_webgpu_context(WGPUInstance *instance_out, WGPUAdapter *adapter_out, WGPUDevice *device_out, WGPUBuffer *staging_buffer_out) {
    struct Results {
        WGPUInstance instance = nullptr;
        WGPUAdapter adapter = nullptr;
        WGPUDevice device = nullptr;
        WGPUBuffer staging_buffer = nullptr;
        bool success = true;
    } results;

    WGPUInstanceDescriptor desc{};
    desc.nextInChain = nullptr;
    results.instance = wgpuCreateInstance(&desc);

    auto request_adapter_callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const *message, void *userdata) {
        auto *results = (Results *)userdata;

        if (status != WGPURequestAdapterStatus_Success) {
            results->success = false;
            return;
        }
        results->adapter = adapter;

        // Use the defaults for most limits.
        WGPURequiredLimits requestedLimits{};
        requestedLimits.nextInChain = nullptr;
        memset(&requestedLimits.limits, 0xFF, sizeof(WGPULimits));

        // TODO: Enable for Emscripten when wgpuAdapterGetLimits is supported.
        // See https://github.com/halide/Halide/issues/7248
#ifdef WITH_DAWN_NATIVE
        WGPUSupportedLimits supportedLimits{};
        supportedLimits.nextInChain = nullptr;
        if (!wgpuAdapterGetLimits(adapter, &supportedLimits)) {
            results->success = false;
            return;
        } else {
            // Raise the limits on buffer size and workgroup storage size.
            requestedLimits.limits.maxBufferSize = supportedLimits.limits.maxBufferSize;
            requestedLimits.limits.maxStorageBufferBindingSize = supportedLimits.limits.maxStorageBufferBindingSize;
            requestedLimits.limits.maxComputeWorkgroupStorageSize = supportedLimits.limits.maxComputeWorkgroupStorageSize;
        }
#endif

        auto device_lost_callback = [](WGPUDeviceLostReason reason,
                                       char const *message,
                                       void *userdata) {
            // Apparently this should not be treated as a fatal error
            if (reason == WGPUDeviceLostReason_Destroyed) {
                return;
            }
            fprintf(stderr, "WGPU Device Lost: %d %s", (int)reason, message);
            abort();
        };

        WGPUDeviceDescriptor desc{};
        desc.nextInChain = nullptr;
        desc.label = nullptr;
#if defined(__EMSCRIPTEN__)
        // ...sigh, really?
        desc.requiredFeaturesCount = 0;
#else
        desc.requiredFeatureCount = 0;
#endif
        desc.requiredFeatures = nullptr;
        desc.requiredLimits = &requestedLimits;
        desc.deviceLostCallback = device_lost_callback;

        auto request_device_callback = [](WGPURequestDeviceStatus status,
                                          WGPUDevice device,
                                          char const *message,
                                          void *userdata) {
            auto *results = (Results *)userdata;
            if (status != WGPURequestDeviceStatus_Success) {
                results->success = false;
                return;
            }
            results->device = device;

            // Create a staging buffer for transfers.
            constexpr int kStagingBufferSize = 4 * 1024 * 1024;
            WGPUBufferDescriptor desc{};
            desc.nextInChain = nullptr;
            desc.label = nullptr;
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            desc.size = kStagingBufferSize;
            desc.mappedAtCreation = false;
            results->staging_buffer = wgpuDeviceCreateBuffer(device, &desc);
            if (results->staging_buffer == nullptr) {
                results->success = false;
                return;
            }
        };

        wgpuAdapterRequestDevice(adapter, &desc, request_device_callback, userdata);
    };

    wgpuInstanceRequestAdapter(results.instance, nullptr, request_adapter_callback, &results);

    // Wait for device initialization to complete.
    while (!results.device && results.success) {
        // TODO: Use wgpuInstanceProcessEvents() when it is supported.
        // See https://github.com/halide/Halide/issues/7248
#ifndef WITH_DAWN_NATIVE
        emscripten_sleep(10);
#else
        usleep(1000);
#endif
    }

    *instance_out = results.instance;
    *adapter_out = results.adapter;
    *device_out = results.device;
    *staging_buffer_out = results.staging_buffer;
    return results.success;
}

inline void destroy_webgpu_context(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUBuffer staging_buffer) {
    wgpuBufferRelease(staging_buffer);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuInstanceRelease(instance);
}

#endif

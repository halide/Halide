#include "HalideRuntimeWebGPU.h"
#include "device_interface.h"
#include "gpu_context_common.h"
#include "printer.h"
#include "scoped_spin_lock.h"

#include "mini_webgpu.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace WebGPU {

extern WEAK halide_device_interface_t webgpu_device_interface;

WEAK int create_webgpu_context(void *user_context);

// A WebGPU instance/adapter/device defined in this module with weak linkage.
WEAK WGPUInstance global_instance = nullptr;
WEAK WGPUAdapter global_adapter = nullptr;
WEAK WGPUDevice global_device = nullptr;
// Lock to synchronize access to the global WebGPU context.
volatile ScopedSpinLock::AtomicFlag WEAK context_lock = 0;

// Size of the staging buffer used for host<->device copies.
constexpr int kWebGpuStagingBufferSize = 4 * 1024 * 1024;
// A staging buffer used for host<->device copies.
WGPUBuffer WEAK staging_buffer = nullptr;

}  // namespace WebGPU
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::WebGPU;

extern "C" {
// TODO: Remove all of this when wgpuInstanceProcessEvents() is supported.
#ifdef WITH_DAWN_NATIVE
// Defined by Dawn, and used to yield execution to asynchronous commands.
void wgpuDeviceTick(WGPUDevice);
// From <unistd.h>, used to spin-lock while waiting for device initialization.
int usleep(uint32_t);
#else
// Defined by Emscripten, and used to yield execution to asynchronous Javascript
// work in combination with Emscripten's "Asyncify" mechanism.
void emscripten_sleep(unsigned int ms);
// Wrap emscripten_sleep in wgpuDeviceTick() to unify usage with Dawn.
void wgpuDeviceTick(WGPUDevice) {
    emscripten_sleep(1);
}
#endif

// The default implementation of halide_webgpu_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_webgpu_acquire_context should always store a valid
//   instance/adapter/device in instance_ret/adapter_ret/device_ret, or return
//   an error code.
// - A call to halide_webgpu_acquire_context is followed by a matching call to
//   halide_webgpu_release_context. halide_webgpu_acquire_context should block
//   while a previous call (if any) has not yet been released via
//   halide_webgpu_release_context.
WEAK int halide_webgpu_acquire_context(void *user_context,
                                       WGPUInstance *instance_ret,
                                       WGPUAdapter *adapter_ret,
                                       WGPUDevice *device_ret,
                                       bool create = true) {
    halide_abort_if_false(user_context, &context_lock != nullptr);
    while (__atomic_test_and_set(&context_lock, __ATOMIC_ACQUIRE)) {
    }

    if (create && (global_device == nullptr)) {
        int status = create_webgpu_context(user_context);
        if (status != halide_error_code_success) {
            __atomic_clear(&context_lock, __ATOMIC_RELEASE);
            return status;
        }
    }

    *instance_ret = global_instance;
    *adapter_ret = global_adapter;
    *device_ret = global_device;

    return halide_error_code_success;
}

WEAK int halide_webgpu_release_context(void *user_context) {
    __atomic_clear(&context_lock, __ATOMIC_RELEASE);
    return 0;
}

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace WebGPU {

// Helper object to acquire and release the WebGPU context.
class WgpuContext {
    void *user_context;

public:
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    int error_code = 0;

    ALWAYS_INLINE WgpuContext(void *user_context)
        : user_context(user_context) {
        error_code = halide_webgpu_acquire_context(
            user_context, &instance, &adapter, &device);
        if (error_code == halide_error_code_success) {
            queue = wgpuDeviceGetQueue(device);
        }
    }

    ALWAYS_INLINE ~WgpuContext() {
        if (queue) {
            wgpuQueueRelease(queue);
        }
        halide_webgpu_release_context(user_context);
    }
};

// Helper class for handling asynchronous errors for a set of WebGPU API calls
// within a particular scope.
class ErrorScope {
public:
    ALWAYS_INLINE ErrorScope(void *user_context, WGPUDevice device)
        : user_context(user_context), device(device) {
        // Capture validation and OOM errors.
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
        wgpuDevicePushErrorScope(device, WGPUErrorFilter_OutOfMemory);
        callbacks_remaining = 2;
    }

    ALWAYS_INLINE ~ErrorScope() {
        if (callbacks_remaining > 0) {
            // Pop the error scopes to flush any pending errors.
            wait();
        }
    }

    // Wait for all error callbacks in this scope to fire.
    // Returns the error code (or success).
    int wait() {
        if (callbacks_remaining == 0) {
            error(user_context) << "no outstanding error scopes\n";
            return halide_error_code_internal_error;
        }

        error_code = halide_error_code_success;
        wgpuDevicePopErrorScope(device, error_callback, this);
        wgpuDevicePopErrorScope(device, error_callback, this);

        // Wait for the error callbacks to fire.
        while (__atomic_load_n(&callbacks_remaining, __ATOMIC_ACQUIRE) > 0) {
            wgpuDeviceTick(device);
        }

        return error_code;
    }

private:
    void *user_context;
    WGPUDevice device;

    // The error code reported by the callback functions.
    volatile int error_code;

    // Used to track outstanding error callbacks.
    volatile int callbacks_remaining = 0;

    // The error callback function.
    // Logs any errors, and decrements the remaining callback count.
    static void error_callback(WGPUErrorType type,
                               char const *message,
                               void *userdata) {
        ErrorScope *context = (ErrorScope *)userdata;
        switch (type) {
        case WGPUErrorType_NoError:
            // Do not overwrite the error_code to avoid masking earlier errors.
            break;
        case WGPUErrorType_Validation:
            error(context->user_context) << "WGPU: validation error: "
                                         << message << "\n";
            context->error_code = halide_error_code_generic_error;
            break;
        case WGPUErrorType_OutOfMemory:
            error(context->user_context) << "WGPU: out-of-memory error: "
                                         << message << "\n";
            context->error_code = halide_error_code_out_of_memory;
            break;
        default:
            error(context->user_context) << "WGPU: unknown error (" << type
                                         << "): " << message << "\n";
            context->error_code = halide_error_code_generic_error;
        }

        __atomic_sub_fetch(&context->callbacks_remaining, 1, __ATOMIC_RELEASE);
    }
};

// A cache for compiled WGSL shader modules.
WEAK Halide::Internal::GPUCompilationCache<WGPUDevice, WGPUShaderModule>
    shader_cache;

namespace {

halide_error_code_t init_error_code = halide_error_code_success;

void request_device_callback(WGPURequestDeviceStatus status,
                             WGPUDevice device,
                             char const *message,
                             void *user_context) {
    if (status != WGPURequestDeviceStatus_Success) {
        debug(user_context) << "wgpuAdapterRequestDevice failed ("
                            << status << "): " << message << "\n";
        init_error_code = halide_error_code_generic_error;
        return;
    }
    global_device = device;
}

void request_adapter_callback(WGPURequestAdapterStatus status,
                              WGPUAdapter adapter,
                              char const *message,
                              void *user_context) {
    if (status != WGPURequestAdapterStatus_Success) {
        debug(user_context) << "wgpuInstanceRequestAdapter failed: ("
                            << status << "): " << message << "\n";
        init_error_code = halide_error_code_generic_error;
        return;
    }
    global_adapter = adapter;

    WGPUDeviceDescriptor desc = {
        .nextInChain = nullptr,
        .label = nullptr,
        .requiredFeaturesCount = 0,
        .requiredFeatures = nullptr,
        .requiredLimits = nullptr,
    };
    wgpuAdapterRequestDevice(adapter, &desc, request_device_callback,
                             user_context);
}

}  // namespace

WEAK int create_webgpu_context(void *user_context) {
    // TODO: Unify this when Emscripten implements wgpuCreateInstance().
#ifdef WITH_DAWN_NATIVE
    WGPUInstanceDescriptor desc{
        .nextInChain = nullptr,
    };
    global_instance = wgpuCreateInstance(&desc);
#else
    global_instance = nullptr;
#endif

    wgpuInstanceRequestAdapter(
        global_instance, nullptr, request_adapter_callback, user_context);

    // Wait for device initialization to complete.
    while (!global_device && init_error_code == halide_error_code_success) {
        // TODO: Use wgpuInstanceProcessEvents() when it is supported.
#ifndef WITH_DAWN_NATIVE
        emscripten_sleep(10);
#else
        usleep(1000);
#endif
    }

    return init_error_code;
}

}  // namespace WebGPU
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::WebGPU;

extern "C" {

WEAK int halide_webgpu_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "WGPU: halide_webgpu_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ErrorScope error_scope(user_context, context.device);

    WGPUBufferDescriptor desc = {
        .nextInChain = nullptr,
        .label = nullptr,
        .usage = WGPUBufferUsage_Storage |
                 WGPUBufferUsage_CopyDst |
                 WGPUBufferUsage_CopySrc,
        .size = buf->size_in_bytes(),
        .mappedAtCreation = false,
    };
    WGPUBuffer device_buffer = wgpuDeviceCreateBuffer(context.device, &desc);

    int error_code = error_scope.wait();
    if (error_code != halide_error_code_success) {
        return error_code;
    }

    if (staging_buffer == nullptr) {
        ErrorScope error_scope(user_context, context.device);

        // Create a staging buffer for transfers if we haven't already.
        WGPUBufferDescriptor desc = {
            .nextInChain = nullptr,
            .label = nullptr,
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
            .size = kWebGpuStagingBufferSize,
            .mappedAtCreation = false,
        };
        staging_buffer = wgpuDeviceCreateBuffer(global_device, &desc);

        int error_code = error_scope.wait();
        if (error_code != halide_error_code_success) {
            staging_buffer = nullptr;
            return error_code;
        }
    }

    buf->device = (uint64_t)device_buffer;
    buf->device_interface = &webgpu_device_interface;
    buf->device_interface->impl->use_module();

    debug(user_context)
        << "      Allocated device buffer " << (void *)buf->device << "\n";

    return halide_error_code_success;
}

WEAK int halide_webgpu_device_free(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }

    WGPUBuffer buffer = (WGPUBuffer)buf->device;

    debug(user_context)
        << "WGPU: halide_webgpu_device_free (user_context: " << user_context
        << ", buf: " << buf << ") WGPUBuffer: " << buffer << "\n";

    wgpuBufferRelease(buffer);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;

    return halide_error_code_success;
}

WEAK int halide_webgpu_device_sync(void *user_context, halide_buffer_t *) {
    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ErrorScope error_scope(user_context, context.device);

    // Wait for all work on the queue to finish.
    struct WorkDoneResult {
        volatile bool complete = false;
        volatile WGPUQueueWorkDoneStatus status;
    };
    WorkDoneResult result;
    wgpuQueueOnSubmittedWorkDone(
        context.queue, 0,
        [](WGPUQueueWorkDoneStatus status, void *userdata) {
            WorkDoneResult *result = (WorkDoneResult *)userdata;
            result->status = status;
            __atomic_store_n(&result->complete, true, __ATOMIC_RELEASE);
        },
        &result);

    int error_code = error_scope.wait();
    if (error_code != halide_error_code_success) {
        return error_code;
    }

    while (!__atomic_load_n(&result.complete, __ATOMIC_ACQUIRE)) {
        wgpuDeviceTick(context.device);
    }

    return result.status == WGPUQueueWorkDoneStatus_Success ?
               halide_error_code_success :
               halide_error_code_device_sync_failed;
}

WEAK int halide_webgpu_device_release(void *user_context) {
    debug(user_context)
        << "WGPU: halide_webgpu_device_release (user_context: " << user_context
        << ")\n";

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    if (staging_buffer) {
        wgpuBufferRelease(staging_buffer);
    }
    wgpuDeviceRelease(context.device);
    wgpuAdapterRelease(context.adapter);
    // TODO: Unify this when Emscripten implements wgpuInstanceRelease().
#ifdef WITH_DAWN_NATIVE
    wgpuInstanceRelease(context.instance);
#endif

    return 1;
}

WEAK int halide_webgpu_copy_to_host(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "WGPU: halide_webgpu_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    // TODO: Handle multi-dimensional strided copies.

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ErrorScope error_scope(user_context, context.device);

    WGPUBuffer buffer = (WGPUBuffer)buf->device;

    // Copy chunks via the staging buffer.
    for (size_t offset = 0; offset < buf->size_in_bytes();
         offset += kWebGpuStagingBufferSize) {

        size_t num_bytes = kWebGpuStagingBufferSize;
        if (offset + num_bytes > buf->size_in_bytes()) {
            num_bytes = buf->size_in_bytes() - offset;
        }

        // Copy this chunk to the staging buffer.
        WGPUCommandEncoder encoder =
            wgpuDeviceCreateCommandEncoder(context.device, nullptr);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, offset,
                                             staging_buffer, 0, num_bytes);
        WGPUCommandBuffer command_buffer =
            wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(context.queue, 1, &command_buffer);

        struct BufferMapResult {
            volatile bool map_complete = false;
            volatile WGPUBufferMapAsyncStatus map_status;
        };
        BufferMapResult result;

        // Map the staging buffer for reading.
        wgpuBufferMapAsync(
            staging_buffer, WGPUMapMode_Read, 0, num_bytes,
            [](WGPUBufferMapAsyncStatus status, void *userdata) {
                BufferMapResult *result = (BufferMapResult *)userdata;
                result->map_status = status;
                __atomic_store_n(&result->map_complete, true, __ATOMIC_RELEASE);
            },
            &result);
        while (!__atomic_load_n(&result.map_complete, __ATOMIC_ACQUIRE)) {
            wgpuDeviceTick(context.device);
        }
        if (result.map_status != WGPUBufferMapAsyncStatus_Success) {
            debug(user_context) << "wgpuBufferMapAsync failed: "
                                << result.map_status << "\n";
            return halide_error_code_copy_to_host_failed;
        }

        // Copy the data from the mapped staging buffer to the host allocation.
        const void *src = wgpuBufferGetConstMappedRange(staging_buffer, 0,
                                                        num_bytes);
        memcpy(buf->host + offset, src, num_bytes);
        wgpuBufferUnmap(staging_buffer);
    }

    return error_scope.wait();
}

WEAK int halide_webgpu_copy_to_device(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "WGPU: halide_webgpu_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    // TODO: Handle multi-dimensional strided copies.

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ErrorScope error_scope(user_context, context.device);

    WGPUBuffer buffer = (WGPUBuffer)buf->device;
    wgpuQueueWriteBuffer(context.queue, buffer, 0, buf->host,
                         buf->size_in_bytes());

    return error_scope.wait();
}

WEAK int halide_webgpu_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                   const struct halide_device_interface_t *dst_device_interface,
                                   struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_crop(void *user_context,
                                   const struct halide_buffer_t *src,
                                   struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_slice(void *user_context,
                                    const struct halide_buffer_t *src,
                                    int slice_dim,
                                    int slice_pos,
                                    struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_release_crop(void *user_context,
                                           struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t mem) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_detach_native(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_initialize_kernels(void *user_context, void **state_ptr, const char *src, int size) {
    debug(user_context)
        << "WGPU: halide_webgpu_initialize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", program: " << (void *)src
        << ", size: " << size << ")\n";

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    // Get the shader module from the cache, compiling it if necessary.
    WGPUShaderModule shader_module;
    if (!shader_cache.kernel_state_setup(
            user_context, state_ptr, context.device, shader_module,
            [&]() -> WGPUShaderModule {
                ErrorScope error_scope(user_context, context.device);

                WGPUShaderModuleWGSLDescriptor wgsl_desc = {
                    .chain = {
                        .next = nullptr,
                        .sType = WGPUSType_ShaderModuleWGSLDescriptor,
                    },
                    .source = src,
                };
                WGPUShaderModuleDescriptor desc = {
                    .nextInChain = (const WGPUChainedStruct *)(&wgsl_desc),
                    .label = nullptr,
                };
                WGPUShaderModule shader_module =
                    wgpuDeviceCreateShaderModule(context.device, &desc);

                int error_code = error_scope.wait();
                if (error_code != halide_error_code_success) {
                    return nullptr;
                }

                return shader_module;
            })) {
        return halide_error_code_generic_error;
    }
    halide_abort_if_false(user_context, shader_module != nullptr);

    return halide_error_code_success;
}

WEAK void halide_webgpu_finalize_kernels(void *user_context, void *state_ptr) {
    debug(user_context)
        << "CL: halide_webgpu_finalize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr << "\n";

    WgpuContext context(user_context);
    if (context.error_code == halide_error_code_success) {
        shader_cache.release_hold(user_context, context.device, state_ptr);
    }
}

WEAK int halide_webgpu_run(void *user_context,
                           void *state_ptr,
                           const char *entry_name,
                           int groupsX, int groupsY, int groupsZ,
                           int threadsX, int threadsY, int threadsZ,
                           int workgroup_mem_bytes,
                           size_t arg_sizes[],
                           void *args[],
                           int8_t arg_is_buffer[]) {
    debug(user_context)
        << "WGPU: halide_webgpu_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ", "
        << "groups: " << groupsX << "x" << groupsY << "x" << groupsZ << ", "
        << "threads: " << threadsX << "x" << threadsY << "x" << threadsZ << ", "
        << "workgroup_mem: " << workgroup_mem_bytes << "\n";

    WgpuContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ErrorScope error_scope(user_context, context.device);

    WGPUShaderModule shader_module = nullptr;
    bool found = shader_cache.lookup(context.device, state_ptr, shader_module);
    halide_abort_if_false(user_context, found && shader_module != nullptr);

    // TODO: Add support for workgroup memory via a pipeline-overridable
    // workgroup storage array.
    halide_abort_if_false(user_context, workgroup_mem_bytes == 0);

    // Create the compute pipeline.
    WGPUProgrammableStageDescriptor stage_desc = {
        .nextInChain = nullptr,
        .module = shader_module,
        .entryPoint = entry_name,
        .constantCount = 0,
        .constants = nullptr,
    };
    WGPUComputePipelineDescriptor pipeline_desc = {
        .nextInChain = nullptr,
        .label = nullptr,
        .layout = nullptr,
        .compute = stage_desc,
    };
    WGPUComputePipeline pipeline =
        wgpuDeviceCreateComputePipeline(context.device, &pipeline_desc);

    // Set up a compute shader dispatch command.
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(context.device, nullptr);
    WGPUComputePassEncoder pass =
        wgpuCommandEncoderBeginComputePass(encoder, nullptr);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);

    // Process function arguments.
    uint32_t num_args = 0;
    uint32_t num_buffers = 0;
    uint32_t uniform_size = 0;
    while (arg_sizes[num_args] != 0) {
        if (arg_is_buffer[num_args]) {
            num_buffers++;
        } else {
            // TODO: Support non-buffer args with different sizes.
            halide_abort_if_false(user_context, arg_sizes[num_args] == 4);
            uniform_size += arg_sizes[num_args];
        }
        num_args++;
    }
    if (num_buffers > 0) {
        // Set up a bind group entry for each buffer argument.
        WGPUBindGroupEntry *bind_group_entries =
            (WGPUBindGroupEntry *)malloc(
                num_buffers * sizeof(WGPUBindGroupEntry));
        for (uint32_t i = 0, b = 0; i < num_args; i++) {
            if (arg_is_buffer[i]) {
                halide_buffer_t *buffer = (halide_buffer_t *)args[i];
                bind_group_entries[b] = WGPUBindGroupEntry{
                    .nextInChain = nullptr,
                    .binding = i,
                    .buffer = (WGPUBuffer)(buffer->device),
                    .offset = 0,
                    .size = buffer->size_in_bytes(),
                    .sampler = nullptr,
                    .textureView = nullptr,
                };
                b++;
            }
        }

        // Create a bind group for the buffer arguments.
        WGPUBindGroupLayout layout =
            wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupDescriptor bindgroup_desc = {
            .nextInChain = nullptr,
            .label = nullptr,
            .layout = layout,
            .entryCount = num_buffers,
            .entries = bind_group_entries,
        };
        WGPUBindGroup bind_group =
            wgpuDeviceCreateBindGroup(context.device, &bindgroup_desc);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, nullptr);
        wgpuBindGroupRelease(bind_group);
        wgpuBindGroupLayoutRelease(layout);

        free(bind_group_entries);
    }
    if (num_args > num_buffers) {
        // Create a uniform buffer for the non-buffer arguments.
        WGPUBufferDescriptor desc = {
            .nextInChain = nullptr,
            .label = nullptr,
            .usage = WGPUBufferUsage_Uniform,
            .size = uniform_size,
            .mappedAtCreation = true,
        };
        WGPUBuffer arg_buffer = wgpuDeviceCreateBuffer(context.device, &desc);

        // Write the argument values to the uniform buffer.
        uint32_t *arg_values =
            (uint32_t *)wgpuBufferGetMappedRange(arg_buffer, 0, uniform_size);
        for (uint32_t a = 0, i = 0; a < num_args; a++) {
            if (arg_is_buffer[a]) {
                continue;
            }
            // TODO: Support non-buffer args with different sizes.
            halide_abort_if_false(user_context, arg_sizes[a] == 4);
            arg_values[i] = *(((uint32_t **)args)[a]);
            i++;
        }
        wgpuBufferUnmap(arg_buffer);

        // Create a bind group for the uniform buffer.
        WGPUBindGroupLayout layout =
            wgpuComputePipelineGetBindGroupLayout(pipeline, 1);
        WGPUBindGroupEntry entry = {
            .nextInChain = nullptr,
            .binding = 0,
            .buffer = arg_buffer,
            .offset = 0,
            .size = uniform_size,
            .sampler = nullptr,
            .textureView = nullptr,
        };
        WGPUBindGroupDescriptor bindgroup_desc = {
            .nextInChain = nullptr,
            .label = nullptr,
            .layout = layout,
            .entryCount = 1,
            .entries = &entry,
        };
        WGPUBindGroup bind_group =
            wgpuDeviceCreateBindGroup(context.device, &bindgroup_desc);
        wgpuComputePassEncoderSetBindGroup(pass, 1, bind_group, 0, nullptr);
        wgpuBindGroupRelease(bind_group);
        wgpuBindGroupLayoutRelease(layout);

        wgpuBufferRelease(arg_buffer);
    }

    wgpuComputePassEncoderDispatch(pass, groupsX, groupsY, groupsZ);
    wgpuComputePassEncoderEndPass(pass);

    // Submit the compute command.
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(context.queue, 1, &commands);

    wgpuCommandEncoderRelease(encoder);
    wgpuComputePipelineRelease(pipeline);

    return error_scope.wait();
}

WEAK const struct halide_device_interface_t *halide_webgpu_device_interface() {
    return &webgpu_device_interface;
}

namespace {
WEAK __attribute__((destructor)) void halide_webgpu_cleanup() {
    halide_webgpu_device_release(nullptr);
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace WebGPU {

WEAK halide_device_interface_impl_t webgpu_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_webgpu_device_malloc,
    halide_webgpu_device_free,
    halide_webgpu_device_sync,
    halide_webgpu_device_release,
    halide_webgpu_copy_to_host,
    halide_webgpu_copy_to_device,
    halide_webgpu_device_and_host_malloc,
    halide_webgpu_device_and_host_free,
    halide_webgpu_buffer_copy,
    halide_webgpu_device_crop,
    halide_webgpu_device_slice,
    halide_webgpu_device_release_crop,
    halide_webgpu_wrap_native,
    halide_webgpu_detach_native,
};

WEAK halide_device_interface_t webgpu_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,
    &webgpu_device_interface_impl};

}  // namespace WebGPU
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

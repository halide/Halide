#include "runtime_internal.h"
#include "scoped_spin_lock.h"
#include "device_interface.h"
#include "HalideRuntimeMetal.h"

extern "C" {

typedef void *objc_id;
typedef void *objc_sel;
extern objc_id objc_getClass(const char *name);
extern objc_sel sel_getUid(const char *str);
extern objc_id objc_msgSend(objc_id self, objc_sel op, ...);

typedef objc_id NSError;
typedef objc_id MTLDevice;
typedef objc_id MTLBuffer;
typedef objc_id MTLCommandQueue;
typedef objc_id MTLCommandBuffer;
typedef objc_id MTLComputeCommandEncoder;
typedef objc_id MTLBlitCommandEncoder;
typedef objc_id MTLComputePipelineState;
typedef objc_id MTLLibrary;
typedef objc_id MTLFunction;
typedef objc_id MTLCompileOptions;

extern MTLDevice MTLCreateSystemDefaultDevice();

}

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

void release_metal_object(objc_id obj) {
    objc_msgSend(obj, sel_getUid("release"));
}

MTLBuffer new_buffer(MTLDevice device, size_t length) {
    return objc_msgSend(device, sel_getUid("newBufferWithLength:options:"),
                        length, 0 /* MTLResourceOptionCPUCacheModeDefault */);
}

MTLCommandQueue new_command_queue(MTLDevice device) {
    return objc_msgSend(device, sel_getUid("newComandQueue"));
}
    
MTLCommandBuffer new_command_buffer(MTLCommandQueue queue) {
    return objc_msgSend(queue, sel_getUid("newComandBuffer"));
}

MTLComputeCommandEncoder new_compute_command_encoder(MTLCommandBuffer buffer) {
    return objc_msgSend(buffer, sel_getUid("newComputeCommandEncoder"));
}

MTLComputePipelineState new_compute_pipeline_state_with_function(MTLDevice device, MTLFunction function) {
    NSError *error_return;
    // TODO: do something with error.
    return objc_msgSend(device, sel_getUid("newComputePipelineStateWithFunction:"), function, &error_return);
}

void set_compute_pipeline_state(MTLComputeCommandEncoder encoder, MTLComputePipelineState pipeline_state) {
    objc_msgSend(encoder, sel_getUid("setComputePipelineState:"), pipeline_state);
}

void end_encoding(MTLComputeCommandEncoder encoder) {
    objc_msgSend(encoder, sel_getUid("endEncoding"));
}

MTLLibrary new_library_with_source(MTLDevice device, const char *source, size_t source_len) {
    NSError *error_return;
    const int NSUTF8StringEncoding = 4;
    objc_id source_str =
        objc_msgSend(objc_msgSend(objc_getClass("NSString"),
                                  sel_getUid("alloc")),
                     sel_getUid("initWithBytesNoCopy:length:encoding:freeWhenDone"), source, source_len, NSUTF8StringEncoding, 0 /* NO */);
    MTLCompileOptions options = objc_msgSend(objc_msgSend(objc_getClass("MTLCompileOptions"), sel_getUid("alloc")),
                                             sel_getUid("setFastMathEnabled"), 1);
    // TODO: handle error.
    MTLLibrary result = objc_msgSend(device, sel_getUid("newLibraryWithSource:options:error:"), source_str, options, &error_return);
    objc_msgSend(options, sel_getUid("release"));
    objc_msgSend(source_str, sel_getUid("release"));
    return result;
}

MTLFunction new_function_with_name(MTLLibrary library, const char *name, size_t name_len) {
    const int NSUTF8StringEncoding = 4;
    objc_id name_str =
        objc_msgSend(objc_msgSend(objc_getClass("NSString"),
                                  sel_getUid("alloc")),
                     sel_getUid("initWithBytesNoCopy:length:encoding:freeWhenDone"), name, name_len, NSUTF8StringEncoding, 0 /* NO */);
    // TODO: handle error.
    MTLFunction result = objc_msgSend(library, sel_getUid("newFunctionWithName:"), name_str);
    objc_msgSend(name_str, sel_getUid("release"));
    return result;
}

void set_input_buffer(MTLComputeCommandEncoder encoder, MTLBuffer input, uint32_t index) {
    objc_msgSend(encoder, sel_getUid("setBuffer:offset:atIndex:"), input, (uint32_t)0, index);
}

void set_threadgroup_memory_length(MTLComputeCommandEncoder encoder, uint32_t index, uint32_t length) {
    objc_msgSend(encoder, sel_getUid("setThreadgroupMemoryLength::atIndex:"), length, index);
}

struct MTLSize {
    unsigned long width;
    unsigned long height;
    unsigned long depth;
};

void dispatch_threadgroups(MTLComputeCommandEncoder encoder, MTLSize threadgroupsPerGrid, MTLSize threadsPerThreadgroup) {
    // TODO: Verify the argument passing here is correct.
    objc_msgSend(encoder, sel_getUid("dispatchThreadgroups:threadsPerThreadgroup:"),
                 threadgroupsPerGrid, threadsPerThreadgroup);
}

void commit_command_buffer(MTLCommandBuffer buffer) {
    objc_msgSend(buffer, sel_getUid("commit"));
}

void wait_until_completed(MTLCommandBuffer buffer) {
    objc_msgSend(buffer, sel_getUid("waitUntilCompleted"));
}

void *buffer_contents(MTLBuffer buffer) {
    return (void *)objc_msgSend(buffer, sel_getUid("contents"));
}

extern WEAK halide_device_interface metal_device_interface;

volatile int WEAK thread_lock = 0;
WEAK MTLDevice device;
WEAK MTLCommandQueue queue;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    MTLLibrary library;
    module_state *next;
};
WEAK module_state *state_list = NULL;

// TODO: Move this to common code?
WEAK size_t buf_size(void *user_context, buffer_t *buf) {
    size_t size = buf->elem_size;
    for (size_t i = 0; i < sizeof(buf->stride) / sizeof(buf->stride[0]); i++) {
        size_t total_dim_size =
            buf->elem_size * buf->extent[i] * buf->stride[i];
        if (total_dim_size > size) {
            size = total_dim_size;
        }
    }
    halide_assert(user_context, size);
    return size;
}

// The default implementation of halide_metal_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_acquire_cl_context should always store a valid context/command
//   queue in ctx/q, or return an error code.
// - A call to halide_acquire_cl_context is followed by a matching call to
//   halide_release_cl_context. halide_acquire_cl_context should block while a
//   previous call (if any) has not yet been released via halide_release_cl_context.
WEAK int halide_metal_acquire_context(void *user_context, MTLDevice &device_ret,
                                      MTLCommandQueue &queue_ret, bool create = true) {
    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

    if (device == 0 && create) {
        device = MTLCreateSystemDefaultDevice();
        if (device == 0) {
            error(user_context) << "Metal: cannot allocate system default device.\n";
            __sync_lock_release(&thread_lock);
            return -1;
        }
        queue = new_command_queue(device);
        if (queue == 0) {
            error(user_context) << "Metal: cannot allocate command queue.\n";
            release_metal_object(device);
            device = 0;
            __sync_lock_release(&thread_lock);
            return -1;
        }
    }

    // If the context has not been initialized, initialize it now.
    halide_assert(user_context, queue != 0);

    device_ret = device;
    queue_ret = queue;
    return 0;
}

WEAK int halide_metal_release_context(void *user_context) {
    __sync_lock_release(&thread_lock);
    return 0;
}

}}}} // namespace Halide::Runtime::Internal::Metal

using namespace Halide::Runtime::Internal::Metal;

extern "C" {

WEAK int halide_metal_device_malloc(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "halide_metal_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    size_t size = buf_size(user_context, buf);
    if (buf->dev) {
        // This buffer already has a device allocation
        return 0;
    }

    halide_assert(user_context, buf->stride[0] >= 0 && buf->stride[1] >= 0 &&
                                buf->stride[2] >= 0 && buf->stride[3] >= 0);

    debug(user_context) << "    allocating buffer of " << (uint64_t)size << " bytes, "
                        << "extents: "
                        << buf->extent[0] << "x"
                        << buf->extent[1] << "x"
                        << buf->extent[2] << "x"
                        << buf->extent[3] << " "
                        << "strides: "
                        << buf->stride[0] << "x"
                        << buf->stride[1] << "x"
                        << buf->stride[2] << "x"
                        << buf->stride[3] << " "
                        << "(" << buf->elem_size << " bytes per element)\n";

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    MTLDevice device;
    MTLCommandQueue queue;

    int acquire_error = halide_metal_acquire_context(user_context, device, queue, true);
    if (acquire_error != 0) {
        return acquire_error;
    }

    MTLBuffer metal_buf = new_buffer(device, size);
    if (metal_buf == 0) {
      error(user_context) << "Metal: Failed to allocate buffer of size " << (int64_t)size << ".\n";
        return -1;
    }

    buf->dev = halide_new_device_wrapper((uint64_t)metal_buf, &metal_device_interface);
    if (buf->dev == 0) {
        error(user_context) << "Metal: out of memory allocating device wrapper.\n";
        release_metal_object(metal_buf);
        halide_metal_release_context(user_context);
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    halide_metal_release_context(user_context);

    return 0;
}


WEAK int halide_metal_device_free(void *user_context, buffer_t* buf) {
    if (buf->dev == 0) {
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    MTLBuffer metal_buf = (MTLBuffer)halide_get_device_handle(buf->dev);
    release_metal_object(metal_buf);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_initialize_kernels(void *user_context, void **state_ptr, const char* source, int source_size) {
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    // Create the state object if necessary. This only happens once, regardless
    // of how many times halide_initialize_kernels/halide_release is called.
    // halide_release traverses this list and releases the module objects, but
    // it does not modify the list nodes created/inserted here.
    module_state **state = (module_state**)state_ptr;
    if (!(*state)) {
        *state = (module_state*)malloc(sizeof(module_state));
        (*state)->library = NULL;
        (*state)->next = state_list;
        state_list = *state;
    }

    MTLDevice device;
    MTLCommandQueue queue;
    int acquire_error = halide_metal_acquire_context(user_context, device, queue);
    if (acquire_error != 0) {
        return acquire_error;
    }

    if ((*state)->library == 0) {
        (*state)->library = new_library_with_source(device, source, source_size);
        if ((*state)->library == 0) {
            error(user_context) << "Metal: new_library_with_source failed.\n";
            halide_metal_release_context(user_context);
            return -1;
        }
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    halide_metal_release_context(user_context);

    return 0;
}

WEAK int halide_metal_device_sync(void *user_context, struct buffer_t *) {
    MTLDevice device;
    MTLCommandQueue queue;
    int acquire_error = halide_metal_acquire_context(user_context, device, queue);
    if (acquire_error != 0) {
        return acquire_error;
    }

    MTLCommandBuffer nop_buffer = new_command_buffer(queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);
    return 0;
}

WEAK int halide_metal_device_release(void *user_context) {
    return -1;
}

WEAK int halide_metal_copy_to_device(void *user_context, buffer_t* buf) {
    MTLDevice device;
    MTLCommandQueue queue;
    int acquire_error = halide_metal_acquire_context(user_context, device, queue);
    if (acquire_error != 0) {
        return acquire_error;
    }

    MTLCommandBuffer nop_buffer = new_command_buffer(queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);

    halide_assert(user_context, buf->host && buf->dev);

    MTLBuffer metal_buf = (MTLBuffer)halide_get_device_handle(buf->dev);
    void *buffer_memory = buffer_contents(metal_buf);
    // TODO: can we just call memcpy here or do we have to do the strided copy?
    memcpy(buffer_memory, buf->host, buf_size(user_context, buf));

    return 0;
}

WEAK int halide_metal_copy_to_host(void *user_context, buffer_t* buf) {
    MTLDevice device;
    MTLCommandQueue queue;
    int acquire_error = halide_metal_acquire_context(user_context, device, queue);
    if (acquire_error != 0) {
        return acquire_error;
    }

    MTLCommandBuffer nop_buffer = new_command_buffer(queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);

    halide_assert(user_context, buf->host && buf->dev);

    MTLBuffer metal_buf = (MTLBuffer)halide_get_device_handle(buf->dev);
    void *buffer_memory = buffer_contents(metal_buf);
    // TODO: can we just call memcpy here or do we have to do the strided copy?
    memcpy(buf->host, buffer_memory, buf_size(user_context, buf));

    return 0;
}

WEAK int halide_metal_run(void *user_context,
                           void *state_ptr,
                           const char* entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void* args[],
                           int8_t arg_is_buffer[],
                           int num_attributes,
                           float* vertex_buffer,
                           int num_coords_dim0,
                           int num_coords_dim1) {
    MTLDevice device;
    MTLCommandQueue queue;
    int acquire_error = halide_metal_acquire_context(user_context, device, queue);
    if (acquire_error != 0) {
        return acquire_error;
    }

    MTLCommandBuffer command_buffer = new_command_buffer(queue);
    if (command_buffer == 0) {
        error(user_context) << "Metal: Could not allocate command buffer.\n";
        halide_metal_release_context(user_context);
        return -1;
    }

    MTLComputeCommandEncoder encoder = new_compute_command_encoder(command_buffer);
    if (encoder == 0) {
        error(user_context) << "Metal: Could not allocate compute command encoder.\n";
        release_metal_object(command_buffer);
        halide_metal_release_context(user_context);
        return -1;
    }

    halide_assert(user_context, state_ptr);
    module_state *state = (module_state*)state_ptr;

    MTLFunction function = new_function_with_name(state->library, entry_name, strlen(entry_name));
    if (function == 0) {
        error(user_context) << "Metal: Could not get function " << entry_name << "from Metal library.\n";
        release_metal_object(encoder);
        release_metal_object(command_buffer);
        halide_metal_release_context(user_context);
        return -1;
    }

    MTLComputePipelineState pipeline_state = new_compute_pipeline_state_with_function(device, function);
    if (pipeline_state == 0) {
        error(user_context) << "Metal: Could not allocate pipeline state.\n";
        release_metal_object(function);
        release_metal_object(encoder);
        release_metal_object(command_buffer);
        halide_metal_release_context(user_context);
        return -1;
    }
    set_compute_pipeline_state(encoder, pipeline_state);
    release_metal_object(pipeline_state);
    release_metal_object(function);

    size_t total_args_size = 0;
    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (!arg_is_buffer[i]) {
            // Metal requires natural alignment for all types in structures.
            // Assert arg_size is exactly a power of two and adjust size to start
            // on the next multiple of that power of two.
            halide_assert(user_context, (arg_sizes[i] & (arg_sizes[i] - 1)) == 0);
            total_args_size = (total_args_size + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
            total_args_size += arg_sizes[i];
        }
    }

    int32_t buffer_index = 0;
    if (total_args_size > 0) {
        MTLBuffer args_buffer = new_buffer(device, total_args_size);
        if (args_buffer == 0) {
            error(user_context) << "Metal: Could not allocate arguments buffer.\n";
            release_metal_object(encoder);
            release_metal_object(command_buffer);
            halide_metal_release_context(user_context);
            return -1;
        }
        char *args_ptr = (char *)buffer_contents(args_buffer);
        size_t offset = 0;
        for (size_t i = 0; arg_sizes[i] != 0; i++) {
            if (!arg_is_buffer[i]) {
                memcpy(&args_ptr[offset], args[i], arg_sizes[i]);
                offset = (offset + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
                offset += arg_sizes[i];
            }
        }
        halide_assert(user_context, offset == total_args_size);
        set_input_buffer(encoder, args_buffer, buffer_index);
        release_metal_object(args_buffer);
        buffer_index++;
    }

    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (arg_is_buffer[i]) {
            halide_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            MTLBuffer metal_buffer = (MTLBuffer)halide_get_device_handle(*(uint64_t *)args[i]);
            set_input_buffer(encoder, metal_buffer, buffer_index);
            buffer_index++;
        }
    }
    set_threadgroup_memory_length(encoder, shared_mem_bytes, 0);

    MTLSize blocks = { blocksX, blocksY, blocksZ };
    MTLSize threads = { threadsX, threadsY, threadsZ };

    dispatch_threadgroups(encoder, blocks, threads);
    end_encoding(encoder);
    release_metal_object(encoder);

    commit_command_buffer(command_buffer);
    release_metal_object(command_buffer);

    halide_metal_release_context(user_context);

    return 0;
}

#if 0 // TODO: get naming right
WEAK int halide_metal_wrap_cl_mem(void *user_context, struct buffer_t *buf, uintptr_t mem) {
}

WEAK uintptr_t halide_metal_detach_cl_mem(void *user_context, struct buffer_t *buf) {
}

WEAK uintptr_t halide_metal_get_cl_mem(void *user_context, struct buffer_t *buf) {
}
#endif

WEAK const struct halide_device_interface *halide_metal_device_interface() {
    return &metal_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_metal_cleanup() {
    halide_metal_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {
WEAK halide_device_interface metal_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_metal_device_malloc,
    halide_metal_device_free,
    halide_metal_device_sync,
    halide_metal_device_release,
    halide_metal_copy_to_host,
    halide_metal_copy_to_device,
};

}}}} // namespace Halide::Runtime::Internal::Metal

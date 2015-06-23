#include "runtime_internal.h"
#include "scoped_spin_lock.h"
#include "device_interface.h"
#include "HalideRuntimeMetal.h"

#include "objc_apple_metal_stubs.h"

extern "C" {
typedef void *objc_id;
typedef void *objc_sel;
extern objc_id objc_getClass(const char *name);
extern objc_sel sel_getUid(const char *str);
extern objc_id objc_msgSend(objc_id self, objc_sel op, ...);
}

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

extern WEAK halide_device_interface metal_device_interface;

volatile int WEAK thread_lock = 0;
WEAK mtl_device *device;
WEAK mtl_command_queue *queue;

// Structure to hold the state of a module attached to the context.
// Also used as a linked-list to keep track of all the different
// modules that are attached to a context in order to release them all
// when then context is released.
struct module_state {
    mtl_library *library;
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
WEAK int halide_metal_acquire_context(void *user_context, mtl_device *&device_ret,
                                      mtl_command_queue *&queue_ret, bool create = true) {
    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

    if (device == 0 && create) {
        device = system_default_device();
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

class MetalContextHolder {
    void *user_context;
    void *autorelease_pool;

public:
    mtl_device *device;
    mtl_command_queue *queue;
    int error;

    MetalContextHolder(void *user_context, bool create) : user_context(user_context) {
        autorelease_pool =
            objc_msgSend(objc_msgSend(objc_getClass("NSAutoreleasePool"),
                                      sel_getUid("alloc")), sel_getUid("init"));
        error = halide_metal_acquire_context(user_context, device, queue, create);
    }

    ~MetalContextHolder() {
        halide_metal_release_context(user_context);
        objc_msgSend(autorelease_pool, sel_getUid("drain"));
    }

};

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

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    mtl_buffer *metal_buf = new_buffer(metal_context.device, size);
    if (metal_buf == 0) {
        error(user_context) << "Metal: Failed to allocate buffer of size " << (int64_t)size << ".\n";
        return -1;
    }

    buf->dev = halide_new_device_wrapper((uint64_t)metal_buf, &metal_device_interface);
    if (buf->dev == 0) {
        error(user_context) << "Metal: out of memory allocating device wrapper.\n";
        release_metal_object(metal_buf);
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}


WEAK int halide_metal_device_free(void *user_context, buffer_t* buf) {
    if (buf->dev == 0) {
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    mtl_buffer *metal_buf = (mtl_buffer *)halide_get_device_handle(buf->dev);
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

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    if ((*state)->library == 0) {
        (*state)->library = new_library_with_source(metal_context.device, source, source_size);
        if ((*state)->library == 0) {
            error(user_context) << "Metal: new_library_with_source failed.\n";
            return -1;
        }
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_device_sync(void *user_context, struct buffer_t *) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    mtl_command_buffer *nop_buffer = new_command_buffer(metal_context.queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);

    return 0;
}

WEAK int halide_metal_device_release(void *user_context) {
    return -1;
}

WEAK int halide_metal_copy_to_device(void *user_context, buffer_t* buf) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    mtl_command_buffer *nop_buffer = new_command_buffer(queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);

    halide_assert(user_context, buf->host && buf->dev);

    mtl_buffer *metal_buf = (mtl_buffer *)halide_get_device_handle(buf->dev);
    void *buffer_memory = buffer_contents(metal_buf);
    // TODO: can we just call memcpy here or do we have to do the strided copy?
    memcpy(buffer_memory, buf->host, buf_size(user_context, buf));

    return 0;
}

WEAK int halide_metal_copy_to_host(void *user_context, buffer_t* buf) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    mtl_command_buffer *nop_buffer = new_command_buffer(metal_context.queue);
    commit_command_buffer(nop_buffer);
    wait_until_completed(nop_buffer);

    halide_assert(user_context, buf->host && buf->dev);

    mtl_buffer *metal_buf = (mtl_buffer *)halide_get_device_handle(buf->dev);
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
    // Buy an autorelease pool because this is not perf critical and it is the
    // really safe thing to do.

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    mtl_command_buffer *command_buffer = new_command_buffer(metal_context.queue);
    if (command_buffer == 0) {
        error(user_context) << "Metal: Could not allocate command buffer.\n";
        return -1;
    }

    mtl_compute_command_encoder *encoder = new_compute_command_encoder(command_buffer);
    if (encoder == 0) {
        error(user_context) << "Metal: Could not allocate compute command encoder.\n";
        return -1;
    }

    halide_assert(user_context, state_ptr);
    module_state *state = (module_state*)state_ptr;

    mtl_function *function = new_function_with_name(state->library, entry_name, strlen(entry_name));
    if (function == 0) {
        error(user_context) << "Metal: Could not get function " << entry_name << "from Metal library.\n";
        return -1;
    }

    mtl_compute_pipeline_state *pipeline_state = new_compute_pipeline_state_with_function(metal_context.device, function);
    if (pipeline_state == 0) {
        error(user_context) << "Metal: Could not allocate pipeline state.\n";
        release_metal_object(function);
        return -1;
    }
    set_compute_pipeline_state(encoder, pipeline_state);

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
        mtl_buffer *args_buffer = new_buffer(metal_context.device, total_args_size);
        if (args_buffer == 0) {
            error(user_context) << "Metal: Could not allocate arguments buffer.\n";
            release_metal_object(pipeline_state);
            release_metal_object(function);
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
            mtl_buffer *metal_buffer = (mtl_buffer *)halide_get_device_handle(*(uint64_t *)args[i]);
            set_input_buffer(encoder, metal_buffer, buffer_index);
            buffer_index++;
        }
    }
    set_threadgroup_memory_length(encoder, shared_mem_bytes, 0);

    dispatch_threadgroups(encoder,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);
    end_encoding(encoder);

    commit_command_buffer(command_buffer);

    release_metal_object(pipeline_state);
    release_metal_object(function);

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

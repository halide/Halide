#include "HalideRuntimeMetal.h"
#include "scoped_spin_lock.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"

#include "objc_support.h"

#include "metal_objc_platform_dependent.h"

extern "C" {
extern objc_id MTLCreateSystemDefaultDevice();
extern struct ObjectiveCClass _NSConcreteGlobalBlock;
}

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

typedef halide_metal_device mtl_device;
typedef halide_metal_command_queue mtl_command_queue;
struct mtl_buffer;
struct mtl_command_buffer;
struct mtl_compute_command_encoder;
struct mtl_blit_command_encoder;
struct mtl_compute_pipeline_state;
struct mtl_library;
struct mtl_function;
struct mtl_compile_options;

WEAK mtl_buffer *new_buffer(mtl_device *device, size_t length) {
    typedef mtl_buffer *(*new_buffer_method)(objc_id device, objc_sel sel, size_t length, size_t options);
    new_buffer_method method = (new_buffer_method)&objc_msgSend;
    return (*method)(device, sel_getUid("newBufferWithLength:options:"),
                     length, 0  /* MTLResourceCPUCacheModeDefaultCache | MTLResourceStorageModeShared */);
}

WEAK mtl_command_queue *new_command_queue(mtl_device *device) {
    return (mtl_command_queue *)objc_msgSend(device, sel_getUid("newCommandQueue"));
}

WEAK mtl_command_buffer *new_command_buffer(mtl_command_queue *queue) {
    return (mtl_command_buffer *)objc_msgSend(queue, sel_getUid("commandBuffer"));
}

WEAK void add_command_buffer_completed_handler(mtl_command_buffer *command_buffer, struct command_buffer_completed_handler_block_literal *handler) {
    typedef void (*add_completed_handler_method)(objc_id command_buffer, objc_sel sel, struct command_buffer_completed_handler_block_literal *handler);
    add_completed_handler_method method = (add_completed_handler_method)&objc_msgSend;
    (*method)(command_buffer, sel_getUid("addCompletedHandler:"), handler);
}

WEAK objc_id command_buffer_error(mtl_command_buffer *buffer) {
    return objc_msgSend(buffer, sel_getUid("error"));
}

WEAK mtl_compute_command_encoder *new_compute_command_encoder(mtl_command_buffer *buffer) {
    return (mtl_compute_command_encoder *)objc_msgSend(buffer, sel_getUid("computeCommandEncoder"));
}

WEAK mtl_blit_command_encoder *new_blit_command_encoder(mtl_command_buffer *buffer) {
    return (mtl_blit_command_encoder *)objc_msgSend(buffer, sel_getUid("blitCommandEncoder"));
}

WEAK mtl_compute_pipeline_state *new_compute_pipeline_state_with_function(mtl_device *device, mtl_function *function) {
    objc_id error_return;
    typedef mtl_compute_pipeline_state *(*new_compute_pipeline_state_method)(objc_id device, objc_sel sel,
                                                                             objc_id function, objc_id *error_return);
    new_compute_pipeline_state_method method = (new_compute_pipeline_state_method)&objc_msgSend;
    mtl_compute_pipeline_state *result = (*method)(device, sel_getUid("newComputePipelineStateWithFunction:error:"),
                                                   function, &error_return);
    if (result == NULL) {
        ns_log_object(error_return);
    }

    return result;
}

WEAK void set_compute_pipeline_state(mtl_compute_command_encoder *encoder, mtl_compute_pipeline_state *pipeline_state) {
    typedef void (*set_compute_pipeline_state_method)(objc_id encoder, objc_sel sel, objc_id pipeline_state);
    set_compute_pipeline_state_method method = (set_compute_pipeline_state_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setComputePipelineState:"), pipeline_state);
}

WEAK void end_encoding(mtl_compute_command_encoder *encoder) {
    objc_msgSend(encoder, sel_getUid("endEncoding"));
}

struct NSRange {
    size_t location;
    size_t length;
};

WEAK void did_modify_range(mtl_buffer *buffer, NSRange range) {
    typedef void (*did_modify_range_method)(objc_id obj, objc_sel sel, NSRange range);
    did_modify_range_method method = (did_modify_range_method)&objc_msgSend;
    (*method)(buffer, sel_getUid("didModifyRange:"), range);
}

WEAK void synchronize_resource(mtl_blit_command_encoder *encoder, mtl_buffer *buffer) {
    typedef void (*synchronize_resource_method)(objc_id obj, objc_sel sel, mtl_buffer *buffer);
    synchronize_resource_method method = (synchronize_resource_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("synchronizeResource:"), buffer);
}

WEAK bool is_buffer_managed(mtl_buffer *buffer) {
    typedef bool (*responds_to_selector_method)(objc_id obj, objc_sel sel_1, objc_sel sel_2);
    responds_to_selector_method method1 = (responds_to_selector_method)&objc_msgSend;
    objc_sel storage_mode_sel = sel_getUid("storageMode");
    if ((*method1)(buffer, sel_getUid("respondsToSelector:"), storage_mode_sel)) {
        typedef int (*storage_mode_method)(objc_id obj, objc_sel sel);
        storage_mode_method method = (storage_mode_method)&objc_msgSend;
        int storage_mode = (*method)(buffer, storage_mode_sel);
        return storage_mode == 1; // MTLStorageModeManaged
    }
    return false;
}

WEAK void end_encoding(mtl_blit_command_encoder *encoder) {
    objc_msgSend(encoder, sel_getUid("endEncoding"));
}

WEAK mtl_library *new_library_with_source(mtl_device *device, const char *source, size_t source_len) {
    objc_id error_return;
    objc_id source_str = wrap_string_as_ns_string(source, source_len);

    objc_id options = objc_msgSend(objc_getClass("MTLCompileOptions"), sel_getUid("alloc"));
    options = objc_msgSend(options, sel_getUid("init"));
    typedef void (*set_fast_math_method)(objc_id options, objc_sel sel, uint8_t flag);
    set_fast_math_method method1 = (set_fast_math_method)&objc_msgSend;
    (*method1)(options, sel_getUid("setFastMathEnabled:"), false);

    typedef mtl_library *(*new_library_with_source_method)(objc_id device, objc_sel sel, objc_id source, objc_id options, objc_id *error_return);
    new_library_with_source_method method2 = (new_library_with_source_method)&objc_msgSend;
    mtl_library *result = (*method2)(device, sel_getUid("newLibraryWithSource:options:error:"),
                                     source_str, options, &error_return);

    release_ns_object(options);
    release_ns_object(source_str);

    if (result == NULL) {
        ns_log_object(error_return);
    }

    return result;
}

WEAK mtl_function *new_function_with_name(mtl_library *library, const char *name, size_t name_len) {
    objc_id name_str = wrap_string_as_ns_string(name, name_len);
    typedef mtl_function *(*new_function_with_name_method)(objc_id library, objc_sel sel, objc_id name);
    new_function_with_name_method method = (new_function_with_name_method)&objc_msgSend;
    mtl_function *result = (*method)(library, sel_getUid("newFunctionWithName:"), name_str);
    release_ns_object(name_str);
    return result;
}

WEAK void set_input_buffer(mtl_compute_command_encoder *encoder, mtl_buffer *input_buffer, uint32_t index) {
    typedef void (*set_buffer_method)(objc_id encoder, objc_sel sel,
                                      mtl_buffer *input_buffer, size_t offset, size_t index);
    set_buffer_method method = (set_buffer_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setBuffer:offset:atIndex:"),
              input_buffer, 0, index);
}

WEAK void set_threadgroup_memory_length(mtl_compute_command_encoder *encoder, uint32_t length, uint32_t index) {
    typedef void (*set_threadgroup_memory_length_method)(objc_id encoder, objc_sel sel,
                                                         size_t length, size_t index);
    set_threadgroup_memory_length_method method = (set_threadgroup_memory_length_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setThreadgroupMemoryLength:atIndex:"),
              length, index);
}

WEAK void commit_command_buffer(mtl_command_buffer *buffer) {
    objc_msgSend(buffer, sel_getUid("commit"));
}

WEAK void wait_until_completed(mtl_command_buffer *buffer) {
    objc_msgSend(buffer, sel_getUid("waitUntilCompleted"));
}

WEAK void *buffer_contents(mtl_buffer *buffer) {
    return objc_msgSend(buffer, sel_getUid("contents"));
}

extern WEAK halide_device_interface_t metal_device_interface;

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

}}}}

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::Metal;

extern "C" {

// The default implementation of halide_metal_acquire_context uses the global
// pointers above, and serializes access with a spin lock.
// Overriding implementations of acquire/release must implement the following
// behavior:
// - halide_acquire_metal_context should always store a valid device/command
//   queue in device/q, or return an error code.
// - A call to halide_acquire_metal_context is followed by a matching call to
//   halide_release_metal_context. halide_acquire_metal_context should block while a
//   previous call (if any) has not yet been released via halide_release_metal_context.
WEAK int halide_metal_acquire_context(void *user_context, mtl_device **device_ret,
                                      mtl_command_queue **queue_ret, bool create) {
    halide_assert(user_context, &thread_lock != NULL);
    while (__sync_lock_test_and_set(&thread_lock, 1)) { }

#ifdef DEBUG_RUNTIME
        halide_start_clock(user_context);
#endif

    if (device == 0 && create) {
        debug(user_context) <<  "Metal - Allocating: MTLCreateSystemDefaultDevice\n";
        device = (mtl_device *)MTLCreateSystemDefaultDevice();
        if (device == 0) {
            error(user_context) << "Metal: cannot allocate system default device.\n";
            __sync_lock_release(&thread_lock);
            return -1;
        }
        debug(user_context) <<  "Metal - Allocating: new_command_queue\n";
        queue = new_command_queue(device);
        if (queue == 0) {
            error(user_context) << "Metal: cannot allocate command queue.\n";
            release_ns_object(device);
            device = 0;
            __sync_lock_release(&thread_lock);
            return -1;
        }
    }

    // If the device has already been initialized,
    // ensure the queue has as well.
    halide_assert(user_context, (device == 0) || (queue != 0));

    *device_ret = device;
    *queue_ret = queue;
    return 0;
}

WEAK int halide_metal_release_context(void *user_context) {
    __sync_lock_release(&thread_lock);
    return 0;
}

} // extern "C"

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

class MetalContextHolder {
    objc_id pool;
    void *user_context;

    // Define these out-of-line as WEAK, to avoid LLVM error "MachO doesn't support COMDATs"
    void save(void *user_context, bool create);
    void restore();

public:
    mtl_device *device;
    mtl_command_queue *queue;
    int error;

    __attribute__((always_inline)) MetalContextHolder(void *user_context, bool create) { save(user_context, create); }
    __attribute__((always_inline)) ~MetalContextHolder() { restore(); }
};

WEAK void MetalContextHolder::save(void *user_context_arg, bool create) {
    user_context = user_context_arg;
    pool = create_autorelease_pool();
    error = halide_metal_acquire_context(user_context, &device, &queue, create);
}

WEAK void MetalContextHolder::restore() {
    halide_metal_release_context(user_context);
    drain_autorelease_pool(pool);
}

struct command_buffer_completed_handler_block_descriptor_1 {
    unsigned long reserved;
    unsigned long block_size;
};

struct command_buffer_completed_handler_block_literal {
    void *isa;
    int flags;
    int reserved;
    void (*invoke)(command_buffer_completed_handler_block_literal *, mtl_command_buffer *buffer);
    struct command_buffer_completed_handler_block_descriptor_1 *descriptor;
};

WEAK command_buffer_completed_handler_block_descriptor_1 command_buffer_completed_handler_descriptor = {
    0, sizeof(command_buffer_completed_handler_block_literal)
};

WEAK void command_buffer_completed_handler_invoke(command_buffer_completed_handler_block_literal *block, mtl_command_buffer *buffer) {
    objc_id buffer_error = command_buffer_error(buffer);
    if (buffer_error != NULL) {
        ns_log_object(buffer_error);
        release_ns_object(buffer_error);
    }
}

WEAK command_buffer_completed_handler_block_literal command_buffer_completed_handler_block = {
    &_NSConcreteGlobalBlock,
    (1 << 28) | (1 << 29), // BLOCK_IS_GLOBAL | BLOCK_HAS_DESCRIPTOR
    0, command_buffer_completed_handler_invoke,
    &command_buffer_completed_handler_descriptor
};

}}}} // namespace Halide::Runtime::Internal::Metal

using namespace Halide::Runtime::Internal::Metal;

extern "C" {

WEAK int halide_metal_device_malloc(void *user_context, buffer_t* buf) {
    debug(user_context)
        << "halide_metal_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

   size_t size = buf_size(buf);
    halide_assert(user_context, size != 0);
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
        release_ns_object(metal_buf);
        return -1;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}


WEAK int halide_metal_device_free(void *user_context, buffer_t* buf) {
    debug(user_context) << "halide_metal_device_free called on buf " << buf << " dev is " << buf->dev << "\n";
    if (buf->dev == 0) {
        return 0;
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    mtl_buffer *metal_buf = (mtl_buffer *)halide_get_device_handle(buf->dev);
    #ifdef DEBUG_RUNTIME
    // If buf->host is not null, it should match the metal_buf's contents.
    if (buf->host) {
        uint8_t *metal_buf_host = (uint8_t *)buffer_contents(metal_buf);
        halide_assert(user_context, buf->host == metal_buf_host);
    }
    #endif
    release_ns_object(metal_buf);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    // If buf->host is not null, it is a pointer gotten from metal_buf... which is no longer
    // valid. Null it out to avoid writes or frees of memory we don't own.
    buf->host = NULL;

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_initialize_kernels(void *user_context, void **state_ptr, const char* source, int source_size) {
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

    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    if ((*state)->library == 0) {
        #ifdef DEBUG_RUNTIME
        uint64_t t_before_compile = halide_current_time_ns(user_context);
        #endif

        debug(user_context) << "Metal - Allocating: new_library_with_source " << (*state)->library << "\n";
        (*state)->library = new_library_with_source(metal_context.device, source, source_size);
        if ((*state)->library == 0) {
            error(user_context) << "Metal: new_library_with_source failed.\n";
            return -1;
        }

        #ifdef DEBUG_RUNTIME
        uint64_t t_after_compile = halide_current_time_ns(user_context);
        debug(user_context) << "Time for halide_metal_initialize_kernels compilation: " << (t_after_compile - t_before_compile) / 1.0e6 << " ms\n";
        #endif
    }

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_initialize_kernels: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

namespace {

WEAK void halide_metal_device_sync_internal(mtl_command_queue *queue, struct buffer_t *buffer) {
    mtl_command_buffer *sync_command_buffer = new_command_buffer(queue);
    if (buffer != NULL) {
        mtl_buffer *metal_buffer = (mtl_buffer *)halide_get_device_handle(buffer->dev);
        if (is_buffer_managed(metal_buffer)) {
            mtl_blit_command_encoder *blit_encoder = new_blit_command_encoder(sync_command_buffer);
            synchronize_resource(blit_encoder, metal_buffer);
            end_encoding(blit_encoder);
        }
    }
    commit_command_buffer(sync_command_buffer);
    wait_until_completed(sync_command_buffer);
}

}

WEAK int halide_metal_device_sync(void *user_context, struct buffer_t *buffer) {
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    halide_metal_device_sync_internal(metal_context.queue, buffer);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_device_sync: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_device_release(void *user_context) {
    // The MetalContext object does not allow the context storage to be modified,
    // so we use halide_metal_acquire_context directly.
    int error;
    mtl_device *acquired_device;
    mtl_command_queue *acquired_queue;
    error = halide_metal_acquire_context(user_context, &acquired_device, &acquired_queue, false);
    if (error != 0) {
        return error;
    }

    if (device) {
        halide_metal_device_sync_internal(queue, NULL);

        // Unload the modules attached to this device. Note that the list
        // nodes themselves are not freed, only the program objects are
        // released. Subsequent calls to halide_init_kernels might re-create
        // the program object using the same list node to store the program
        // object.
        module_state *state = state_list;
        while (state) {
          if (state->library) {
                debug(user_context) << "Metal - Releasing: new_library_with_source " << state->library << "\n";
                release_ns_object(state->library);
                state->library = NULL;
            }
            state = state->next;
        }

        // Release the device itself, if we created it.
        if (acquired_device == device) {
            debug(user_context) <<  "Metal - Releasing: new_command_queue " << queue << "\n";
            release_ns_object(queue);
            queue = NULL;

            debug(user_context) << "Metal - Releasing: MTLCreateSystemDefaultDevice " << device << "\n";
            release_ns_object(device);
            device = NULL;
        }
    }

    halide_metal_release_context(user_context);

    return 0;
}

WEAK int halide_metal_copy_to_device(void *user_context, buffer_t* buffer) {
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    halide_assert(user_context, buffer->host && buffer->dev);

    device_copy c = make_host_to_device_copy(buffer);
    mtl_buffer *metal_buffer = (mtl_buffer *)c.dst;
    c.dst = (uint64_t)buffer_contents(metal_buffer);

    debug(user_context) << "halide_metal_copy_to_device dev = " << (void*)buffer->dev << " metal_buffer = " << metal_buffer << " host = " << buffer->host << "\n";

    c.copy_memory(user_context);

    if (is_buffer_managed(metal_buffer)) {
        size_t total_size = buf_size(buffer);
        halide_assert(user_context, total_size != 0);
        NSRange total_extent;
        total_extent.location = 0;
        total_extent.length = total_size;
        did_modify_range(metal_buffer, total_extent);
    }
    halide_metal_device_sync_internal(metal_context.queue, buffer);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_copy_to_device: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_copy_to_host(void *user_context, buffer_t* buffer) {
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error != 0) {
        return metal_context.error;
    }

    halide_metal_device_sync_internal(metal_context.queue, buffer);

    halide_assert(user_context, buffer->host && buffer->dev);

    device_copy c = make_device_to_host_copy(buffer);
    c.src = (uint64_t)buffer_contents((mtl_buffer *)c.src);

    c.copy_memory(user_context);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_copy_to_host: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

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
    #ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
    #endif

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
        release_ns_object(function);
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
            release_ns_object(pipeline_state);
            release_ns_object(function);
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
        release_ns_object(args_buffer);
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

    // Round shared memory size up to a multiple of 16, as required by setThreadgroupMemoryLength.
    shared_mem_bytes = (shared_mem_bytes + 0xF) & ~0xF;
    debug(user_context) << "Setting shared memory length to " << shared_mem_bytes << "\n";
    set_threadgroup_memory_length(encoder, shared_mem_bytes, 0);

    static int32_t total_dispatches = 0;
    debug(user_context) << "Dispatching threadgroups (number " << total_dispatches++ <<
        ") blocks(" << blocksX << ", " << blocksY << ", " << blocksZ <<
        ") threads(" << threadsX << ", " << threadsY << ", " << threadsZ << ")\n";

    dispatch_threadgroups(encoder,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);
    end_encoding(encoder);

    add_command_buffer_completed_handler(command_buffer, &command_buffer_completed_handler_block);

    commit_command_buffer(command_buffer);

    release_ns_object(pipeline_state);
    release_ns_object(function);

    #ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_device_run: " << (t_after - t_before) / 1.0e6 << " ms\n";
    #endif

    return 0;
}

WEAK int halide_metal_device_and_host_malloc(void *user_context, struct buffer_t *buffer) {
    debug(user_context) << "halide_metal_device_and_host_malloc called.\n";
    int result = halide_metal_device_malloc(user_context, buffer);
    if (result == 0) {
        mtl_buffer *metal_buffer = (mtl_buffer *)halide_get_device_handle(buffer->dev);
        buffer->host = (uint8_t *)buffer_contents(metal_buffer);
        debug(user_context) << "halide_metal_device_and_host_malloc dev = " << (void*)buffer->dev << " metal_buffer = " << metal_buffer << " host = " << buffer->host << "\n";
    }
    return result;
}

WEAK int halide_metal_device_and_host_free(void *user_context, struct buffer_t *buffer) {
    debug(user_context) << "halide_metal_device_and_host_free called.\n";
    halide_metal_device_free(user_context, buffer);
    buffer->host = NULL;
    return 0;
}

WEAK int halide_metal_wrap_buffer(void *user_context, struct buffer_t *buf, uintptr_t buffer) {
    halide_assert(user_context, buf->dev == 0);
    if (buf->dev != 0) {
        return -2;
    }
    buf->dev = halide_new_device_wrapper(buffer, &metal_device_interface);
    if (buf->dev == 0) {
        return -1;
    }
    return 0;
}

WEAK uintptr_t halide_metal_detach_buffer(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &metal_device_interface);
    uint64_t buffer = halide_get_device_handle(buf->dev);
    halide_delete_device_wrapper(buf->dev);
    buf->dev = 0;
    return (uintptr_t)buffer;
}

WEAK uintptr_t halide_metal_get_buffer(void *user_context, struct buffer_t *buf) {
    if (buf->dev == NULL) {
        return 0;
    }
    halide_assert(user_context, halide_get_device_interface(buf->dev) == &metal_device_interface);
    uint64_t buffer = halide_get_device_handle(buf->dev);
    return (uintptr_t)buffer;
}

WEAK const struct halide_device_interface_t *halide_metal_device_interface() {
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
WEAK halide_device_interface_t metal_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_metal_device_malloc,
    halide_metal_device_free,
    halide_metal_device_sync,
    halide_metal_device_release,
    halide_metal_copy_to_host,
    halide_metal_copy_to_device,
    halide_metal_device_and_host_malloc,
    halide_metal_device_and_host_free,
};

}}}} // namespace Halide::Runtime::Internal::Metal

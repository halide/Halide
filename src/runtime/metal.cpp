#include "HalideRuntimeMetal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "gpu_context_common.h"
#include "printer.h"
#include "scoped_spin_lock.h"

#include "objc_support.h"

#include "metal_objc_platform_dependent.h"

extern "C" {
extern objc_id MTLCreateSystemDefaultDevice();
extern struct ObjectiveCClass _NSConcreteGlobalBlock;
void *dlsym(void *, const char *);
#define RTLD_DEFAULT ((void *)-2)
}

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Metal {

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
                     length, 0 /* MTLResourceCPUCacheModeDefaultCache | MTLResourceStorageModeShared */);
}

WEAK mtl_command_queue *new_command_queue(mtl_device *device) {
    typedef mtl_command_queue *(*new_command_queue_method)(objc_id dev, objc_sel sel);
    new_command_queue_method method = (new_command_queue_method)&objc_msgSend;
    return (mtl_command_queue *)(*method)(device, sel_getUid("newCommandQueue"));
}

WEAK mtl_command_buffer *new_command_buffer(mtl_command_queue *queue, const char *label, size_t label_len) {
    objc_id label_str = wrap_string_as_ns_string(label, label_len);

    typedef mtl_command_buffer *(*new_command_buffer_method)(objc_id queue, objc_sel sel);
    new_command_buffer_method method = (new_command_buffer_method)&objc_msgSend;
    mtl_command_buffer *command_buffer = (mtl_command_buffer *)(*method)(queue, sel_getUid("commandBuffer"));

    typedef void (*set_label_method)(objc_id command_buffer, objc_sel sel, objc_id label_string);
    set_label_method method1 = (set_label_method)&objc_msgSend;
    (*method1)(command_buffer, sel_getUid("setLabel:"), label_str);

    release_ns_object(label_str);
    return command_buffer;
}

WEAK void add_command_buffer_completed_handler(mtl_command_buffer *command_buffer, struct command_buffer_completed_handler_block_literal *handler) {
    typedef void (*add_completed_handler_method)(objc_id command_buffer, objc_sel sel, struct command_buffer_completed_handler_block_literal * handler);
    add_completed_handler_method method = (add_completed_handler_method)&objc_msgSend;
    (*method)(command_buffer, sel_getUid("addCompletedHandler:"), handler);
}

WEAK objc_id command_buffer_error(mtl_command_buffer *buffer) {
    typedef objc_id (*error_method)(objc_id buf, objc_sel sel);
    error_method method = (error_method)&objc_msgSend;
    return (*method)(buffer, sel_getUid("error"));
}

WEAK mtl_compute_command_encoder *new_compute_command_encoder(mtl_command_buffer *buffer) {
    typedef mtl_compute_command_encoder *(*compute_command_encoder_method)(objc_id buf, objc_sel sel);
    compute_command_encoder_method method = (compute_command_encoder_method)&objc_msgSend;
    return (mtl_compute_command_encoder *)(*method)(buffer, sel_getUid("computeCommandEncoder"));
}

WEAK mtl_blit_command_encoder *new_blit_command_encoder(mtl_command_buffer *buffer) {
    typedef mtl_blit_command_encoder *(*blit_command_encoder_method)(objc_id buf, objc_sel sel);
    blit_command_encoder_method method = (blit_command_encoder_method)&objc_msgSend;
    return (mtl_blit_command_encoder *)(*method)(buffer, sel_getUid("blitCommandEncoder"));
}

WEAK mtl_compute_pipeline_state *new_compute_pipeline_state_with_function(mtl_device *device, mtl_function *function) {
    objc_id error_return;
    typedef mtl_compute_pipeline_state *(*new_compute_pipeline_state_method)(objc_id device, objc_sel sel,
                                                                             objc_id function, objc_id * error_return);
    new_compute_pipeline_state_method method = (new_compute_pipeline_state_method)&objc_msgSend;
    mtl_compute_pipeline_state *result = (*method)(device, sel_getUid("newComputePipelineStateWithFunction:error:"),
                                                   function, &error_return);
    if (result == nullptr) {
        ns_log_object(error_return);
    }

    return result;
}

WEAK unsigned long get_max_total_threads_per_threadgroup(mtl_compute_pipeline_state *pipeline_state) {
    typedef unsigned long (*get_max_total_threads_per_threadgroup_method)(objc_id pipeline_state, objc_sel sel);
    get_max_total_threads_per_threadgroup_method method = (get_max_total_threads_per_threadgroup_method)&objc_msgSend;
    return (*method)(pipeline_state, sel_getUid("maxTotalThreadsPerThreadgroup"));
}

WEAK void set_compute_pipeline_state(mtl_compute_command_encoder *encoder, mtl_compute_pipeline_state *pipeline_state) {
    typedef void (*set_compute_pipeline_state_method)(objc_id encoder, objc_sel sel, objc_id pipeline_state);
    set_compute_pipeline_state_method method = (set_compute_pipeline_state_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setComputePipelineState:"), pipeline_state);
}

WEAK void end_encoding(mtl_compute_command_encoder *encoder) {
    typedef void (*end_encoding_method)(objc_id encoder, objc_sel sel);
    end_encoding_method method = (end_encoding_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("endEncoding"));
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
    typedef void (*synchronize_resource_method)(objc_id obj, objc_sel sel, mtl_buffer * buffer);
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
        return storage_mode == 1;  // MTLStorageModeManaged
    }
    return false;
}

WEAK void buffer_to_buffer_1d_copy(mtl_blit_command_encoder *encoder,
                                   mtl_buffer *from, size_t from_offset,
                                   mtl_buffer *to, size_t to_offset,
                                   size_t size) {
    typedef void (*copy_from_buffer_method)(objc_id obj, objc_sel sel, objc_id src_buf, size_t s_offset,
                                            objc_id dst_buf, size_t d_offset, size_t s);
    copy_from_buffer_method method = (copy_from_buffer_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:"),
              from, from_offset, to, to_offset, size);
}

WEAK void end_encoding(mtl_blit_command_encoder *encoder) {
    typedef void (*end_encoding_method)(objc_id encoder, objc_sel sel);
    end_encoding_method method = (end_encoding_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("endEncoding"));
}

WEAK bool buffer_supports_set_bytes(mtl_compute_command_encoder *encoder) {
    typedef bool (*responds_to_selector_method)(objc_id obj, objc_sel sel_1, objc_sel sel_2);
    responds_to_selector_method method1 = (responds_to_selector_method)&objc_msgSend;
    objc_sel set_bytes_sel = sel_getUid("setBytes:length:atIndex:");
    return (*method1)(encoder, sel_getUid("respondsToSelector:"), set_bytes_sel);
}

WEAK mtl_library *new_library_with_source(mtl_device *device, const char *source, size_t source_len) {
    objc_id error_return;
    objc_id source_str = wrap_string_as_ns_string(source, source_len);

    typedef objc_id (*options_method)(objc_id obj, objc_sel sel);
    options_method method = (options_method)&objc_msgSend;

    objc_id options = (*method)(objc_getClass("MTLCompileOptions"), sel_getUid("alloc"));
    options = (*method)(options, sel_getUid("init"));
    typedef void (*set_fast_math_method)(objc_id options, objc_sel sel, uint8_t flag);
    set_fast_math_method method1 = (set_fast_math_method)&objc_msgSend;
    (*method1)(options, sel_getUid("setFastMathEnabled:"), false);

    typedef mtl_library *(*new_library_with_source_method)(objc_id device, objc_sel sel, objc_id source, objc_id options, objc_id * error_return);
    new_library_with_source_method method2 = (new_library_with_source_method)&objc_msgSend;
    mtl_library *result = (*method2)(device, sel_getUid("newLibraryWithSource:options:error:"),
                                     source_str, options, &error_return);

    release_ns_object(options);
    release_ns_object(source_str);

    if (result == nullptr) {
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

WEAK void set_input_buffer(mtl_compute_command_encoder *encoder, mtl_buffer *input_buffer, uint64_t offset, uint32_t index) {
    typedef void (*set_buffer_method)(objc_id encoder, objc_sel sel,
                                      mtl_buffer * input_buffer, size_t offset, size_t index);
    set_buffer_method method = (set_buffer_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setBuffer:offset:atIndex:"),
              input_buffer, (size_t)offset, index);
}

WEAK void set_input_buffer_from_bytes(mtl_compute_command_encoder *encoder, uint8_t *input_buffer, uint32_t length, uint32_t index) {
    typedef void (*set_bytes_method)(objc_id encoder, objc_sel sel,
                                     void *input_buffer, size_t length, size_t index);
    set_bytes_method method = (set_bytes_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setBytes:length:atIndex:"),
              input_buffer, length, index);
}

WEAK void set_threadgroup_memory_length(mtl_compute_command_encoder *encoder, uint32_t length, uint32_t index) {
    typedef void (*set_threadgroup_memory_length_method)(objc_id encoder, objc_sel sel,
                                                         size_t length, size_t index);
    set_threadgroup_memory_length_method method = (set_threadgroup_memory_length_method)&objc_msgSend;
    (*method)(encoder, sel_getUid("setThreadgroupMemoryLength:atIndex:"),
              length, index);
}

WEAK void commit_command_buffer(mtl_command_buffer *buffer) {
    typedef void (*commit_method)(objc_id buf, objc_sel sel);
    commit_method method = (commit_method)&objc_msgSend;
    (*method)(buffer, sel_getUid("commit"));
}

WEAK void wait_until_completed(mtl_command_buffer *buffer) {
    typedef void (*wait_until_completed_method)(objc_id buf, objc_sel sel);
    wait_until_completed_method method = (wait_until_completed_method)&objc_msgSend;
    (*method)(buffer, sel_getUid("waitUntilCompleted"));
}

WEAK void *buffer_contents(mtl_buffer *buffer) {
    typedef void *(*contents_method)(objc_id buf, objc_sel sel);
    contents_method method = (contents_method)&objc_msgSend;
    return (void *)(*method)(buffer, sel_getUid("contents"));
}

WEAK void *nsarray_first_object(objc_id arr) {
    typedef objc_id (*nsarray_first_object_method)(objc_id arr, objc_sel sel);
    nsarray_first_object_method method = (nsarray_first_object_method)&objc_msgSend;
    return (*method)(arr, sel_getUid("firstObject"));
}

// MTLCopyAllDevices() is only available for macOS and is
// intended for non-GUI apps.  Newer versions of macOS (10.15+)
// will not return a valid device if MTLCreateSystemDefaultDevice()
// is used from a non-GUI app.
WEAK mtl_device *get_default_mtl_device() {
    mtl_device *device = (mtl_device *)MTLCreateSystemDefaultDevice();
    if (device == nullptr) {
        // We assume Metal.framework is already loaded
        // (call dlsym directly, rather than halide_get_symbol, as we
        // currently don't provide halide_get_symbol for iOS, only OSX)
        void *handle = dlsym(RTLD_DEFAULT, "MTLCopyAllDevices");
        if (handle != nullptr) {
            typedef objc_id (*mtl_copy_all_devices_method)();
            mtl_copy_all_devices_method method = (mtl_copy_all_devices_method)handle;
            objc_id devices = (objc_id)(*method)();
            if (devices != nullptr) {
                device = (mtl_device *)nsarray_first_object(devices);
            }
        }
    }
    return device;
}

extern WEAK halide_device_interface_t metal_device_interface;

volatile ScopedSpinLock::AtomicFlag WEAK thread_lock = 0;
WEAK mtl_device *device;
WEAK mtl_command_queue *queue;

struct device_handle {
    mtl_buffer *buf;
    uint64_t offset;
};

WEAK Halide::Internal::GPUCompilationCache<mtl_device *, mtl_library *> compilation_cache;

// API Capabilities.  If more capabilities need to be checked,
// this can be refactored to something more robust/general.
WEAK bool metal_api_supports_set_bytes;
WEAK mtl_device *metal_api_checked_device;

namespace {
void do_device_to_device_copy(void *user_context, mtl_blit_command_encoder *encoder,
                              const device_copy &c, uint64_t src_offset, uint64_t dst_offset, int d) {
    if (d == 0) {
        buffer_to_buffer_1d_copy(encoder, ((device_handle *)c.src)->buf, c.src_begin + src_offset,
                                 ((device_handle *)c.dst)->buf, dst_offset, c.chunk_size);
    } else {
        // TODO: deal with negative strides. Currently the code in
        // device_buffer_utils.h does not do so either.
        uint64_t src_off = 0, dst_off = 0;
        for (uint64_t i = 0; i < c.extent[d - 1]; i++) {
            do_device_to_device_copy(user_context, encoder, c, src_offset + src_off, dst_offset + dst_off, d - 1);
            dst_off += c.dst_stride_bytes[d - 1];
            src_off += c.src_stride_bytes[d - 1];
        }
    }
}
}  // namespace

}  // namespace Metal
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

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
    halide_debug_assert(user_context, &thread_lock != nullptr);
    while (__atomic_test_and_set(&thread_lock, __ATOMIC_ACQUIRE)) {
    }

#ifdef DEBUG_RUNTIME
    halide_start_clock(user_context);
#endif

    if (device == nullptr && create) {
        debug(user_context) << "Metal - Allocating: MTLCreateSystemDefaultDevice\n";
        device = get_default_mtl_device();
        if (device == nullptr) {
            __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
            error(user_context) << "halide_metal_acquire_context: cannot allocate system default device.";
            return halide_error_code_generic_error;
        }
        debug(user_context) << "Metal - Allocating: new_command_queue\n";
        queue = new_command_queue(device);
        if (queue == nullptr) {
            release_ns_object(device);
            device = nullptr;
            __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
            error(user_context) << "halide_metal_acquire_context: cannot allocate command queue.";
            return halide_error_code_generic_error;
        }
    }

    // If the device has already been initialized,
    // ensure the queue has as well.
    if (device != nullptr && queue == nullptr) {
        error(user_context) << "halide_metal_acquire_context: device initialized but queue is not.";
        return halide_error_code_generic_error;
    }

    *device_ret = device;
    *queue_ret = queue;
    return halide_error_code_success;
}

WEAK int halide_metal_release_context(void *user_context) {
    __atomic_clear(&thread_lock, __ATOMIC_RELEASE);
    return halide_error_code_success;
}

}  // extern "C"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Metal {

class MetalContextHolder {
    objc_id pool;
    void *const user_context;
    int status;  // must always be a valid halide_error_code_t value

public:
    mtl_device *device;
    mtl_command_queue *queue;

    ALWAYS_INLINE MetalContextHolder(void *user_context, bool create)
        : pool(create_autorelease_pool()), user_context(user_context) {
        status = halide_metal_acquire_context(user_context, &device, &queue, create);
    }
    ALWAYS_INLINE ~MetalContextHolder() {
        (void)halide_metal_release_context(user_context);  // ignore errors
        drain_autorelease_pool(pool);
    }

    ALWAYS_INLINE int error() const {
        return status;
    }
};

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
    0, sizeof(command_buffer_completed_handler_block_literal)};

WEAK void command_buffer_completed_handler_invoke(command_buffer_completed_handler_block_literal *block, mtl_command_buffer *buffer) {
    objc_id buffer_error = command_buffer_error(buffer);
    if (buffer_error != nullptr) {
        ns_log_object(buffer_error);
        release_ns_object(buffer_error);
    }
}

WEAK command_buffer_completed_handler_block_literal command_buffer_completed_handler_block = {
    &_NSConcreteGlobalBlock,
    (1 << 28) | (1 << 29),  // BLOCK_IS_GLOBAL | BLOCK_HAS_DESCRIPTOR
    0, command_buffer_completed_handler_invoke,
    &command_buffer_completed_handler_descriptor};

}  // namespace Metal
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::Metal;

extern "C" {

WEAK int halide_metal_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "halide_metal_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    size_t size = buf->size_in_bytes();
    if (size == 0) {
        error(user_context) << "halide_metal_device_malloc: Failed to allocate buffer of size 0.";
        return halide_error_code_generic_error;
    }

    if (buf->device) {
        // This buffer already has a device allocation
        return halide_error_code_success;
    }

    // Check all strides positive
    for (int i = 0; i < buf->dimensions; i++) {
        if (buf->dim[i].stride < 0) {
            error(user_context) << "halide_metal_device_malloc: negatives strides are illegal.";
            return halide_error_code_generic_error;
        }
    }

    debug(user_context) << "    allocating " << *buf << "\n";

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    device_handle *handle = (device_handle *)malloc(sizeof(device_handle));
    if (handle == nullptr) {
        return halide_error_code_out_of_memory;
    }

    mtl_buffer *metal_buf = new_buffer(metal_context.device, size);
    if (metal_buf == nullptr) {
        free(handle);
        error(user_context) << "Metal: Failed to allocate buffer of size " << (int64_t)size << ".\n";
        return halide_error_code_out_of_memory;
    }

    handle->buf = metal_buf;
    handle->offset = 0;

    buf->device = (uint64_t)handle;
    buf->device_interface = &metal_device_interface;
    buf->device_interface->impl->use_module();

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_device_free(void *user_context, halide_buffer_t *buf) {
    debug(user_context) << "halide_metal_device_free called on buf "
                        << buf << " device is " << buf->device << "\n";
    if (buf->device == 0) {
        return halide_error_code_success;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    device_handle *handle = (device_handle *)buf->device;
    if (((device_handle *)buf->device)->offset != 0) {
        error(user_context) << "halide_metal_device_free: halide_metal_device_free called on buffer obtained from halide_device_crop.";
        return halide_error_code_generic_error;
    }

    release_ns_object(handle->buf);
    free(handle);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_initialize_kernels(void *user_context, void **state_ptr, const char *source, int source_size) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    mtl_library *library{};
    const bool setup = compilation_cache.kernel_state_setup(user_context, state_ptr, metal_context.device, library,
                                                            new_library_with_source, metal_context.device,
                                                            source, source_size);
    if (!setup || library == nullptr) {
        error(user_context) << "halide_metal_initialize_kernels: setup failed.\n";
        return halide_error_code_generic_error;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_initialize_kernels: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK void halide_metal_finalize_kernels(void *user_context, void *state_ptr) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error() == halide_error_code_success) {
        compilation_cache.release_hold(user_context, metal_context.device, state_ptr);
    }
}

namespace {

WEAK void halide_metal_device_sync_internal(mtl_command_queue *queue, struct halide_buffer_t *buffer) {
    const char *buffer_label = "halide_metal_device_sync_internal";
    mtl_command_buffer *sync_command_buffer = new_command_buffer(queue, buffer_label, strlen(buffer_label));
    if (buffer != nullptr) {
        mtl_buffer *metal_buffer = ((device_handle *)buffer->device)->buf;
        if (is_buffer_managed(metal_buffer)) {
            mtl_blit_command_encoder *blit_encoder = new_blit_command_encoder(sync_command_buffer);
            synchronize_resource(blit_encoder, metal_buffer);
            end_encoding(blit_encoder);
        }
    }
    commit_command_buffer(sync_command_buffer);
    wait_until_completed(sync_command_buffer);
}

}  // namespace

WEAK int halide_metal_device_sync(void *user_context, struct halide_buffer_t *buffer) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

    halide_metal_device_sync_internal(metal_context.queue, buffer);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_device_sync: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_device_release(void *user_context) {
    // The MetalContext object does not allow the context storage to be modified,
    // so we use halide_metal_acquire_context directly.
    mtl_device *acquired_device;
    mtl_command_queue *acquired_queue;
    auto result = halide_metal_acquire_context(user_context, &acquired_device, &acquired_queue, false);
    if (result) {
        return result;
    }

    if (acquired_device) {
        halide_metal_device_sync_internal(queue, nullptr);

        debug(user_context) << "Calling delete context on device " << acquired_device << "\n";
        compilation_cache.delete_context(user_context, acquired_device, release_ns_object);

        // Release the device itself, if we created it.
        if (acquired_device == device) {
            debug(user_context) << "Metal - Releasing: new_command_queue " << queue << "\n";
            release_ns_object(queue);
            queue = nullptr;

            debug(user_context) << "Metal - Releasing: MTLCreateSystemDefaultDevice " << device << "\n";
            release_ns_object(device);
            device = nullptr;
        }
    }

    return halide_metal_release_context(user_context);
}

WEAK int halide_metal_copy_to_device(void *user_context, halide_buffer_t *buffer) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

    if (!(buffer->host && buffer->device)) {
        error(user_context) << "halide_metal_copy_to_device: either host or device is null.";
        return halide_error_code_generic_error;
    }

    device_copy c = make_host_to_device_copy(buffer);
    mtl_buffer *metal_buffer = ((device_handle *)c.dst)->buf;
    c.dst = (uint64_t)buffer_contents(metal_buffer) + ((device_handle *)c.dst)->offset;

    debug(user_context) << "halide_metal_copy_to_device dev = " << (void *)buffer->device
                        << " metal_buffer = " << metal_buffer
                        << " host = " << buffer->host << "\n";

    copy_memory(c, user_context);

    if (is_buffer_managed(metal_buffer)) {
        size_t total_size = buffer->size_in_bytes();
        halide_debug_assert(user_context, total_size != 0);
        NSRange total_extent;
        total_extent.location = 0;
        total_extent.length = total_size;
        did_modify_range(metal_buffer, total_extent);
    }
    halide_metal_device_sync_internal(metal_context.queue, buffer);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_copy_to_device: "
                        << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_copy_to_host(void *user_context, halide_buffer_t *buffer) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

    halide_metal_device_sync_internal(metal_context.queue, buffer);

    if (!(buffer->host && buffer->device)) {
        error(user_context) << "halide_metal_copy_to_host: either host or device is null.";
        return halide_error_code_generic_error;
    }

    if (buffer->dimensions > MAX_COPY_DIMS) {
        error(user_context) << "halide_metal_copy_to_host: buffer->dimensions > MAX_COPY_DIMS.";
        return halide_error_code_generic_error;
    }

    device_copy c = make_device_to_host_copy(buffer);
    c.src = (uint64_t)buffer_contents(((device_handle *)c.src)->buf) + ((device_handle *)c.src)->offset;

    copy_memory(c, user_context);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_copy_to_host: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_run(void *user_context,
                          void *state_ptr,
                          const char *entry_name,
                          int blocksX, int blocksY, int blocksZ,
                          int threadsX, int threadsY, int threadsZ,
                          int shared_mem_bytes,
                          size_t arg_sizes[],
                          void *args[],
                          int8_t arg_is_buffer[]) {
#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

    mtl_command_buffer *command_buffer = new_command_buffer(metal_context.queue, entry_name, strlen(entry_name));
    if (command_buffer == nullptr) {
        error(user_context) << "Metal: Could not allocate command buffer.";
        return halide_error_code_generic_error;
    }

    mtl_compute_command_encoder *encoder = new_compute_command_encoder(command_buffer);
    if (encoder == nullptr) {
        error(user_context) << "Metal: Could not allocate compute command encoder.";
        return halide_error_code_generic_error;
    }

    mtl_library *library{};
    bool found = compilation_cache.lookup(metal_context.device, state_ptr, library);
    if (!(found && library != nullptr)) {
        error(user_context) << "Metal: cache lookup failed to find library.";
        return halide_error_code_generic_error;
    }

    mtl_function *function = new_function_with_name(library, entry_name, strlen(entry_name));
    if (function == nullptr) {
        error(user_context) << "Metal: Could not get function " << entry_name << "from Metal library.";
        return halide_error_code_generic_error;
    }

    mtl_compute_pipeline_state *pipeline_state = new_compute_pipeline_state_with_function(metal_context.device, function);
    if (pipeline_state == nullptr) {
        error(user_context) << "Metal: Could not allocate pipeline state.";
        return halide_error_code_generic_error;
    }

#ifdef DEBUG_RUNTIME
    int64_t max_total_threads_per_threadgroup = get_max_total_threads_per_threadgroup(pipeline_state);
    if (max_total_threads_per_threadgroup < threadsX * threadsY * threadsZ) {
        end_encoding(encoder);
        release_ns_object(pipeline_state);
        error(user_context) << "Metal: threadsX(" << threadsX << ") * threadsY("
                            << threadsY << ") * threadsZ(" << threadsZ
                            << ") (" << (threadsX * threadsY * threadsZ)
                            << ") must be <= " << max_total_threads_per_threadgroup
                            << ". (device threadgroup size limit)\n";
        return halide_error_code_generic_error;
    }
#endif

    set_compute_pipeline_state(encoder, pipeline_state);

    size_t total_args_size = 0;
    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (!arg_is_buffer[i]) {
            // Metal requires natural alignment for all types in structures.
            // Assert arg_size is exactly a power of two and adjust size to start
            // on the next multiple of that power of two.
            //
            // TODO(zalman): This seems fishy - if the arguments are
            // not already sorted in decreasing order of size, wrong
            // results occur. To repro, remove the sorting code in CodeGen_GPU_Host
            halide_debug_assert(user_context, (arg_sizes[i] & (arg_sizes[i] - 1)) == 0);
            total_args_size = (total_args_size + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
            total_args_size += arg_sizes[i];
        }
    }

    int32_t buffer_index = 0;
    if (total_args_size > 0) {
        mtl_buffer *args_buffer = nullptr;  // used if the total arguments size large
        uint8_t small_args_buffer[4096];    // used if the total arguments size is small
        char *args_ptr;

        if (metal_api_checked_device != metal_context.device) {
            metal_api_supports_set_bytes = buffer_supports_set_bytes(encoder);
            metal_api_checked_device = metal_context.device;
            if (metal_api_supports_set_bytes) {
                debug(user_context) << "Metal - supports setBytes\n";
            }
        }

        // The Metal compiler introduces padding up to a multiple of 4 bytes
        // in the struct, per email communication from Apple
        size_t padded_args_size = (total_args_size + 4 - 1) & ~((size_t)(4 - 1));
        debug(user_context) << "Total args size is " << (uint64_t)total_args_size << " and with padding, size is " << (uint64_t)padded_args_size << "\n";
        halide_debug_assert(user_context, padded_args_size >= total_args_size);

        if (padded_args_size < 4096 && metal_api_supports_set_bytes) {
            args_ptr = (char *)small_args_buffer;
        } else {
            args_buffer = new_buffer(metal_context.device, padded_args_size);
            if (args_buffer == nullptr) {
                release_ns_object(pipeline_state);
                error(user_context) << "Metal: Could not allocate arguments buffer.";
                return halide_error_code_generic_error;
            }
            args_ptr = (char *)buffer_contents(args_buffer);
        }
        size_t offset = 0;
        for (size_t i = 0; arg_sizes[i] != 0; i++) {
            if (!arg_is_buffer[i]) {
                memcpy(&args_ptr[offset], args[i], arg_sizes[i]);
                offset = (offset + arg_sizes[i] - 1) & ~(arg_sizes[i] - 1);
                offset += arg_sizes[i];
            }
        }
        halide_debug_assert(user_context, offset == total_args_size);
        if (total_args_size < 4096 && metal_api_supports_set_bytes) {
            set_input_buffer_from_bytes(encoder, small_args_buffer,
                                        padded_args_size, buffer_index);
        } else {
            set_input_buffer(encoder, args_buffer, 0, buffer_index);
            release_ns_object(args_buffer);
        }
        buffer_index++;
    }

    for (size_t i = 0; arg_sizes[i] != 0; i++) {
        if (arg_is_buffer[i]) {
            halide_debug_assert(user_context, arg_sizes[i] == sizeof(uint64_t));
            device_handle *handle = (device_handle *)((halide_buffer_t *)args[i])->device;
            set_input_buffer(encoder, handle->buf, handle->offset, buffer_index);
            buffer_index++;
        }
    }

    // Round shared memory size up to a multiple of 16, as required by setThreadgroupMemoryLength.
    shared_mem_bytes = (shared_mem_bytes + 0xF) & ~0xF;
    debug(user_context) << "Setting shared memory length to " << shared_mem_bytes << "\n";
    set_threadgroup_memory_length(encoder, shared_mem_bytes, 0);

    static int32_t total_dispatches = 0;
    debug(user_context) << "Dispatching threadgroups (number " << total_dispatches++ << ") blocks(" << blocksX << ", " << blocksY << ", " << blocksZ << ") threads(" << threadsX << ", " << threadsY << ", " << threadsZ << ")\n";

    dispatch_threadgroups(encoder,
                          blocksX, blocksY, blocksZ,
                          threadsX, threadsY, threadsZ);
    end_encoding(encoder);

    add_command_buffer_completed_handler(command_buffer, &command_buffer_completed_handler_block);

    commit_command_buffer(command_buffer);

    // We deliberately don't release the function here; this was causing
    // crashes on Mojave (issues #3395 and #3408).
    // We're still releasing the pipeline state object, as that seems to not
    // cause zombied objects.
    release_ns_object(pipeline_state);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "Time for halide_metal_device_run: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_device_and_host_malloc(void *user_context, struct halide_buffer_t *buffer) {
    debug(user_context) << "halide_metal_device_and_host_malloc called.\n";
    auto result = halide_metal_device_malloc(user_context, buffer);
    if (result) {
        return result;
    }

    mtl_buffer *metal_buffer = ((device_handle *)(buffer->device))->buf;
    buffer->host = (uint8_t *)buffer_contents(metal_buffer);
    debug(user_context) << "halide_metal_device_and_host_malloc"
                        << " device = " << (void *)buffer->device
                        << " metal_buffer = " << metal_buffer
                        << " host = " << buffer->host << "\n";
    return halide_error_code_success;
}

WEAK int halide_metal_device_and_host_free(void *user_context, struct halide_buffer_t *buffer) {
    debug(user_context) << "halide_metal_device_and_host_free called.\n";
    auto result = halide_metal_device_free(user_context, buffer);
    buffer->host = nullptr;
    return result;
}

WEAK int halide_metal_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                  const struct halide_device_interface_t *dst_device_interface,
                                  struct halide_buffer_t *dst) {
    if (dst->dimensions > MAX_COPY_DIMS) {
        error(user_context) << "Buffer has too many dimensions to copy to/from GPU";
        return halide_error_code_device_buffer_copy_failed;
    }

    // We only handle copies to metal buffers or to host
    if (dst_device_interface != nullptr && dst_device_interface != &metal_device_interface) {
        error(user_context) << "halide_metal_buffer_copy: only handle copies to metal buffers or to host";
        return halide_error_code_device_buffer_copy_failed;
    }

    if ((src->device_dirty() || src->host == nullptr) && src->device_interface != &metal_device_interface) {
        halide_debug_assert(user_context, dst_device_interface == &metal_device_interface);
        // This is handled at the higher level.
        return halide_error_code_incompatible_device_interface;
    }

    bool from_host = (src->device_interface != &metal_device_interface) ||
                     (src->device == 0) ||
                     (src->host_dirty() && src->host != nullptr);
    bool to_host = !dst_device_interface;

    if (!(from_host || src->device)) {
        error(user_context) << "halide_metal_buffer_copy: invalid copy source";
        return halide_error_code_device_buffer_copy_failed;
    }
    if (!(to_host || dst->device)) {
        error(user_context) << "halide_metal_buffer_copy: invalid copy destination";
        return halide_error_code_device_buffer_copy_failed;
    }

    device_copy c = make_buffer_copy(src, from_host, dst, to_host);

    {
        MetalContextHolder metal_context(user_context, true);
        if (metal_context.error()) {
            return metal_context.error();
        }

        debug(user_context)
            << "halide_metal_buffer_copy (user_context: " << user_context
            << ", src: " << src << ", dst: " << dst << ")\n";

#ifdef DEBUG_RUNTIME
        uint64_t t_before = halide_current_time_ns(user_context);
#endif

        // Device only case
        if (!from_host && !to_host) {
            debug(user_context) << "halide_metal_buffer_copy device to device case.\n";
            const char *buffer_label = "halide_metal_buffer_copy";
            mtl_command_buffer *blit_command_buffer = new_command_buffer(metal_context.queue, buffer_label, strlen(buffer_label));
            mtl_blit_command_encoder *blit_encoder = new_blit_command_encoder(blit_command_buffer);
            do_device_to_device_copy(user_context, blit_encoder, c, ((device_handle *)c.src)->offset,
                                     ((device_handle *)c.dst)->offset, dst->dimensions);
            end_encoding(blit_encoder);
            commit_command_buffer(blit_command_buffer);
        } else {
            if (!from_host) {
                // Need to make sure all reads and writes to/from source
                // are complete.
                halide_metal_device_sync_internal(metal_context.queue, src);

                c.src = (uint64_t)buffer_contents(((device_handle *)c.src)->buf) + ((device_handle *)c.src)->offset;
            }

            mtl_buffer *dst_buffer;
            if (!to_host) {
                // Need to make sure all reads and writes to/from destination
                // are complete.
                halide_metal_device_sync_internal(metal_context.queue, dst);

                dst_buffer = ((device_handle *)c.dst)->buf;
                halide_debug_assert(user_context, from_host);
                c.dst = (uint64_t)buffer_contents(dst_buffer) + ((device_handle *)c.dst)->offset;
            }

            copy_memory(c, user_context);

            if (!to_host) {
                if (is_buffer_managed(dst_buffer)) {
                    size_t total_size = dst->size_in_bytes();
                    halide_debug_assert(user_context, total_size != 0);
                    NSRange total_extent;
                    total_extent.location = 0;
                    total_extent.length = total_size;
                    did_modify_range(dst_buffer, total_extent);
                }
                // Synchronize as otherwise host source memory might still be read from after return.
                halide_metal_device_sync_internal(metal_context.queue, dst);
            }
        }

#ifdef DEBUG_RUNTIME
        uint64_t t_after = halide_current_time_ns(user_context);
        debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif
    }

    return halide_error_code_success;
}

namespace {

WEAK int metal_device_crop_from_offset(void *user_context,
                                       const struct halide_buffer_t *src,
                                       int64_t offset,
                                       struct halide_buffer_t *dst) {
    MetalContextHolder metal_context(user_context, true);
    if (metal_context.error()) {
        return metal_context.error();
    }

    dst->device_interface = src->device_interface;
    device_handle *new_handle = (device_handle *)malloc(sizeof(device_handle));
    if (new_handle == nullptr) {
        error(user_context) << "halide_metal_device_crop: malloc failed making device handle.";
        return halide_error_code_out_of_memory;
    }

    retain_ns_object(((device_handle *)src->device)->buf);
    new_handle->buf = ((device_handle *)src->device)->buf;
    new_handle->offset = ((device_handle *)src->device)->offset + offset;
    dst->device = (uint64_t)new_handle;
    return halide_error_code_success;
}

}  // namespace

WEAK int halide_metal_device_crop(void *user_context,
                                  const struct halide_buffer_t *src,
                                  struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_crop_byte_offset(src, dst);
    return metal_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_metal_device_slice(void *user_context,
                                   const struct halide_buffer_t *src,
                                   int slice_dim, int slice_pos,
                                   struct halide_buffer_t *dst) {
    const int64_t offset = calc_device_slice_byte_offset(src, slice_dim, slice_pos);
    return metal_device_crop_from_offset(user_context, src, offset, dst);
}

WEAK int halide_metal_device_release_crop(void *user_context,
                                          struct halide_buffer_t *buf) {
    // Basically the same code as in halide_metal_device_free, but with
    // enough differences to require separate code.

    debug(user_context) << "halide_metal_device_release_crop called on buf "
                        << buf << " device is " << buf->device << "\n";
    if (buf->device == 0) {
        return halide_error_code_success;
    }

#ifdef DEBUG_RUNTIME
    uint64_t t_before = halide_current_time_ns(user_context);
#endif

    device_handle *handle = (device_handle *)buf->device;

    release_ns_object(handle->buf);
    free(handle);

#ifdef DEBUG_RUNTIME
    uint64_t t_after = halide_current_time_ns(user_context);
    debug(user_context) << "    Time: " << (t_after - t_before) / 1.0e6 << " ms\n";
#endif

    return halide_error_code_success;
}

WEAK int halide_metal_wrap_buffer(void *user_context, struct halide_buffer_t *buf, uint64_t buffer) {
    if (buf->device != 0) {
        error(user_context) << "halide_metal_wrap_buffer: device field is already non-zero.";
        return halide_error_code_generic_error;
    }
    device_handle *handle = (device_handle *)malloc(sizeof(device_handle));
    if (handle == nullptr) {
        error(user_context) << "halide_metal_wrap_buffer: malloc failed making device handle.";
        return halide_error_code_out_of_memory;
    }
    handle->buf = (mtl_buffer *)buffer;
    handle->offset = 0;

    buf->device = (uint64_t)handle;
    buf->device_interface = &metal_device_interface;
    buf->device_interface->impl->use_module();
    return halide_error_code_success;
}

WEAK int halide_metal_detach_buffer(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return halide_error_code_success;
    }
    if (buf->device_interface != &metal_device_interface) {
        error(user_context) << "halide_metal_detach_buffer: device is not metal.";
        return halide_error_code_generic_error;
    }
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;
    free((device_handle *)buf->device);
    buf->device = 0;
    return halide_error_code_success;
}

WEAK uintptr_t halide_metal_get_buffer(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_debug_assert(user_context, buf->device_interface == &metal_device_interface);
    return (uintptr_t)(((device_handle *)buf->device)->buf);
}

WEAK uint64_t halide_metal_get_crop_offset(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return 0;
    }
    halide_debug_assert(user_context, buf->device_interface == &metal_device_interface);
    return (uint64_t)(((device_handle *)buf->device)->offset);
}

WEAK const struct halide_device_interface_t *halide_metal_device_interface() {
    return &metal_device_interface;
}

namespace {
WEAK __attribute__((destructor)) void halide_metal_cleanup() {
    compilation_cache.release_all(nullptr, release_ns_object);
    (void)halide_metal_device_release(nullptr);  // ignore errors
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Metal {

WEAK halide_device_interface_impl_t metal_device_interface_impl = {
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
    halide_metal_buffer_copy,
    halide_metal_device_crop,
    halide_metal_device_slice,
    halide_metal_device_release_crop,
    halide_metal_wrap_buffer,
    halide_metal_detach_buffer};

WEAK halide_device_interface_t metal_device_interface = {
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
    &metal_device_interface_impl};

}  // namespace Metal
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

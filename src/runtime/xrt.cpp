#include "HalideRuntimeXRT.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "runtime_internal.h"
#include "scoped_mutex_lock.h"

#include "mini_xrt.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace XRT {

extern WEAK halide_device_interface_t xrt_device_interface;

WEAK int create_xrt_context(void *user_context);

// An XRT device defined in this module with weak linkage.
WEAK xrtDeviceHandle global_device = nullptr;

// Lock to synchronize access to the global XRT device.
WEAK halide_mutex thread_lock;

}  // namespace XRT
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal;
using namespace Halide::Runtime::Internal::XRT;

extern "C" {

WEAK int halide_xrt_acquire_context(void *user_context,
                                    void **device_ret,
                                    bool create = true) {
    halide_debug_assert(user_context, &thread_lock != nullptr);

    halide_mutex_lock(&thread_lock);

    if (create && (global_device == nullptr)) {
        int status = create_xrt_context(user_context);
        if (status != halide_error_code_success) {
            halide_mutex_unlock(&thread_lock);
            return status;
        }
    }

    *device_ret = global_device;

    return halide_error_code_success;
}

WEAK int halide_xrt_release_context(void *user_context) {
    halide_mutex_unlock(&thread_lock);
    return halide_error_code_success;
}

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace XRT {

// Helper object to acquire and release the XRT context.
class XRTContext {
    void *user_context;

public:
    xrtDeviceHandle device = nullptr;

    int error_code = 0;

    ALWAYS_INLINE XRTContext(void *user_context)
        : user_context(user_context) {
        error_code = halide_xrt_acquire_context(
            user_context, &device);
        if (error_code == halide_error_code_success) {
            halide_start_clock(user_context);
        }
    }

    ALWAYS_INLINE ~XRTContext() {
        (void)halide_xrt_release_context(user_context);  // ignore errors
    }
};

// XrtBufferHandle represents a device buffer
struct XrtBufferHandle {
    // If nullptr, it means not allocated yet
    xrtBufferHandle handle;
    size_t size;
    bool copy_to_device_pending;
};

// XrtKernelState represents a loaded kernel on the device
struct XrtKernelState {
    xrtKernelHandle handle;
};

WEAK int create_xrt_context(void *user_context) {
    unsigned int count = xclProbe();
    debug(user_context) << "XRT: create_xrt_context: found: " << count << " devices\n";

    if (count == 0) {
        error(user_context) << "XRT: create_xrt_context: error: no devices were found\n";
        return halide_error_code_gpu_device_error;
    }

    for (unsigned i = 0; i < count; i++) {
        xrtDeviceHandle device = xrtDeviceOpen(i);
        debug(user_context) << "XRT: create_xrt_context: xrtDeviceOpen: " << device << "\n";
        if (device != nullptr) {
            global_device = device;
            return halide_error_code_success;
        }
    }

    error(user_context) << "XRT: create_xrt_context: error: could not open any device\n";
    return halide_error_code_gpu_device_error;
}

}  // namespace XRT
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::XRT;

namespace {

int sync_bo_to_device(void *user_context, XrtBufferHandle *handle, const void *host, size_t size) {
    int ret;
    debug(user_context)
        << "sync_bo_to_device: buf->size_in_bytes(): " << (uint64_t)size
        << ", handle->size: " << (uint64_t)handle->size << "\n";

    ret = xrtBOWrite(handle->handle, host, size, 0);
    if (ret) {
        error(user_context) << "XRT: halide_xrt_copy_to_device: xrtBOWrite failed: "
                            << ret << "\n";
        return halide_error_code_generic_error;
    }

    ret = xrtBOSync(handle->handle, XCL_BO_SYNC_BO_TO_DEVICE, size, 0);
    if (ret) {
        error(user_context) << "XRT: halide_xrt_copy_to_device: xrtBOSync failed: "
                            << ret << "\n";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

}  // namespace

extern "C" {

WEAK int halide_xrt_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "XRT: halide_xrt_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->device) {
        return halide_error_code_success;
    }

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    // Buffers are lazily allocated on the device. This is because the memory
    // bank to allocate from is not known until the xclbin is loaded.
    XrtBufferHandle *handle =
        (XrtBufferHandle *)malloc(sizeof(XrtBufferHandle));
    memset(handle, 0, sizeof(*handle));
    handle->handle = nullptr;
    handle->size = buf->size_in_bytes();

    buf->device = (uint64_t)(uintptr_t)handle;
    buf->device_interface = &xrt_device_interface;

    debug(user_context) << "XRT: halide_xrt_device_malloc:"
                        << " lazily allocated device buffer with size: "
                        << (uint64_t)handle->size
                        << ". Descriptor: " << (void *)buf->device << "\n";

    return halide_error_code_success;
}

WEAK int halide_xrt_device_free(void *user_context, halide_buffer_t *buf) {
    if (buf->device == 0) {
        return halide_error_code_success;
    }

    XrtBufferHandle *handle = (XrtBufferHandle *)buf->device;

    debug(user_context)
        << "XRT: halide_xrt_device_free (user_context: " << user_context
        << ", buf: " << buf << ")"
        << "\n";

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    if (handle->handle != nullptr) {
        xrtBOFree(handle->handle);
        handle->handle = nullptr;
    }

    free(handle);
    buf->device = 0;
    buf->device_interface->impl->release_module();
    buf->device_interface = nullptr;

    return halide_error_code_success;
}

WEAK int halide_xrt_device_sync(void *user_context, halide_buffer_t *) {
    debug(user_context)
        << "XRT: halide_xrt_device_sync (user_context: " << user_context
        << ")\n";

    return halide_error_code_generic_error;
}

WEAK int halide_xrt_device_release(void *user_context) {
    debug(user_context)
        << "XRT: halide_xrt_device_release (user_context: " << user_context
        << ")\n";

    // The XRTContext object does not allow the context storage to be modified,
    // so we use halide_acquire_context directly.
    int err;
    xrtDeviceHandle device;
    err = halide_xrt_acquire_context(user_context, &device, false);
    if (err != halide_error_code_success) {
        return err;
    }

    if (device) {
        if (device == global_device) {
            xrtDeviceClose(device);
            global_device = nullptr;
        }
    }

    return halide_xrt_release_context(user_context);
}

WEAK int halide_xrt_device_and_host_malloc(void *user_context,
                                           struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf,
                                                 &xrt_device_interface);
}

WEAK int halide_xrt_device_and_host_free(void *user_context,
                                         struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf,
                                               &xrt_device_interface);
}

WEAK int halide_xrt_buffer_copy(void *user_context,
                                struct halide_buffer_t *src,
                                const struct halide_device_interface_t *dst_device_interface,
                                struct halide_buffer_t *dst) {
    debug(user_context)
        << "XRT: halide_xrt_buffer_copy (user_context: " << user_context
        << ", src: " << src << ", dst: " << dst << ")\n";

    return halide_error_code_generic_error;
}

WEAK int halide_xrt_copy_to_device(void *user_context,
                                   halide_buffer_t *buf) {
    debug(user_context)
        << "XRT: halide_xrt_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    XrtBufferHandle *handle = (XrtBufferHandle *)buf->device;

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    // The copy to device will take place just before launching the kernel.
    handle->copy_to_device_pending = true;

    return halide_error_code_success;
}

WEAK int halide_xrt_copy_to_host(void *user_context,
                                 halide_buffer_t *buf) {
    int ret;
    debug(user_context)
        << "XRT: halide_xrt_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    XrtBufferHandle *handle = (XrtBufferHandle *)buf->device;

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    debug(user_context)
        << "buf->size_in_bytes(): " << (uint64_t)buf->size_in_bytes()
        << ", handle->size: " << (uint64_t)handle->size << "\n";

    ret = xrtBOSync(handle->handle, XCL_BO_SYNC_BO_FROM_DEVICE, buf->size_in_bytes(), 0);
    if (ret) {
        error(user_context) << "XRT: halide_xrt_copy_to_host: xrtBOSync failed: "
                            << ret << "\n";
        return halide_error_code_generic_error;
    }

    ret = xrtBORead(handle->handle, buf->host, buf->size_in_bytes(), 0);
    if (ret) {
        error(user_context) << "XRT: halide_xrt_copy_to_host: xrtBORead failed: "
                            << ret << "\n";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

WEAK int halide_xrt_device_crop(void *user_context,
                                const struct halide_buffer_t *src,
                                struct halide_buffer_t *dst) {
    return halide_error_code_generic_error;
}

WEAK int halide_xrt_device_slice(void *user_context,
                                 const struct halide_buffer_t *src,
                                 int slice_dim,
                                 int slice_pos,
                                 struct halide_buffer_t *dst) {
    return halide_error_code_generic_error;
}

WEAK int halide_xrt_device_release_crop(void *user_context,
                                        struct halide_buffer_t *buf) {
    return halide_error_code_generic_error;
}

WEAK int halide_xrt_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t mem) {
    halide_debug_assert(user_context, false && "unimplemented");
    return halide_error_code_unimplemented;
}

WEAK int halide_xrt_detach_native(void *user_context, halide_buffer_t *buf) {
    halide_debug_assert(user_context, false && "unimplemented");
    return halide_error_code_unimplemented;
}

WEAK int halide_xrt_initialize_kernels(void *user_context, void **state_ptr, const char *kernel_name) {
    int ret;
    char xclbin_name[512];
    xuid_t uuid;
    xrtKernelHandle kernel_handle;
    XrtKernelState *state;

    debug(user_context)
        << "XRT: halide_xrt_initialize_kernels (user_context: " << user_context
        << ", state_ptr: " << state_ptr
        << ", kernel_name: " << kernel_name << ")\n";

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    ret = snprintf(xclbin_name, sizeof(xclbin_name), "%s.xclbin", kernel_name);
    if (ret < 0) {
        error(user_context)
            << "XRT: halide_xrt_initialize_kernels: "
            << "error generating xclbin name\n";
        return halide_error_code_generic_error;
    }

    ret = xrtDeviceLoadXclbinFile(context.device, xclbin_name);
    if (ret != 0) {
        error(user_context)
            << "XRT: halide_xrt_initialize_kernels: "
            << "failed to load xclbin file: " << xclbin_name
            << ", error: " << ret << "\n";
        return halide_error_code_generic_error;
    }

    debug(user_context)
        << "XRT: halide_xrt_initialize_kernels: "
        << "loaded xclbin file: " << xclbin_name << "\n";

    ret = xrtDeviceGetXclbinUUID(context.device, uuid);
    if (ret != 0) {
        error(user_context)
            << "XRT: halide_xrt_initialize_kernels: "
            << "failed to get xclbin uuid, error " << ret << "\n";
        return halide_error_code_generic_error;
    }

    kernel_handle = xrtPLKernelOpen(context.device, uuid, "toplevel");
    if (kernel_handle == XRT_NULL_HANDLE) {
        error(user_context)
            << "XRT: halide_xrt_initialize_kernels: "
            << "failed to open PL kernel\n";
        return halide_error_code_generic_error;
    }

    state = (XrtKernelState *)malloc(sizeof(XrtKernelState));
    state->handle = kernel_handle;

    *state_ptr = state;

    return halide_error_code_success;
}

WEAK void halide_xrt_finalize_kernels(void *user_context, void *state_ptr) {
    XrtKernelState *state;

    debug(user_context)
        << "XRT: halide_xrt_finalize_kernels (user_context: "
        << user_context << ", state_ptr: " << state_ptr << "\n";

    XRTContext context(user_context);
    if (context.error_code == halide_error_code_success) {
        state = (XrtKernelState *)state_ptr;
        xrtKernelClose(state->handle);

        free(state);
    }
}

WEAK int halide_xrt_run(void *user_context,
                        void *state_ptr,
                        const char *entry_name,
                        halide_type_t arg_types[],
                        void *args[],
                        int8_t arg_is_buffer[]) {
    int ret;
    XrtKernelState *state;
    xrtRunHandle run_handle;
    uint32_t num_args;
    uint64_t t_before, t_after;

    debug(user_context)
        << "XRT: halide_xrt_run (user_context: " << user_context << ", "
        << "entry: " << entry_name << ")\n";

    XRTContext context(user_context);
    if (context.error_code) {
        return context.error_code;
    }

    state = (XrtKernelState *)state_ptr;
    run_handle = xrtRunOpen(state->handle);
    if (run_handle == XRT_NULL_HANDLE) {
        error(user_context)
            << "XRT: halide_xrt_run: "
            << "failed to open run handle for kernel: " << entry_name << "\n";
        return halide_error_code_generic_error;
    }

    num_args = 0;
    while (args[num_args] != nullptr) {
        static const char *const type_code_names[] = {
            "int",
            "uint",
            "float",
            "handle",
            "bfloat",
        };

        debug(user_context)
            << "XRT: halide_xrt_run: "
            << "arg[" << num_args << "]: "
            << (arg_is_buffer[num_args] ? "buffer" : "scalar") << ", "
            << "type: " << type_code_names[arg_types[num_args].code] << "\n";

        if (arg_is_buffer[num_args]) {
            halide_buffer_t *buffer = (halide_buffer_t *)args[num_args];
            XrtBufferHandle *buf = (XrtBufferHandle *)buffer->device;

            // Buffer not yet allocated. Allocate it now.
            if (buf->handle == XRT_NULL_HANDLE) {
                buf->handle = xrtBOAlloc(context.device, buf->size, XRT_BO_FLAGS_CACHEABLE,
                                         (xrtMemoryGroup)xrtKernelArgGroupId(state->handle, num_args));
                if (buf->handle == XRT_NULL_HANDLE) {
                    error(user_context)
                        << "XRT: halide_xrt_run: "
                        << "failed to allocate buffer with size: " << (uint64_t)buf->size
                        << " for kernel: " << entry_name << "\n";
                    xrtRunClose(run_handle);
                    return halide_error_code_generic_error;
                }

                debug(user_context) << "XRT: halide_xrt_run: "
                                    << "allocated buffer with size: " << (uint64_t)buf->size
                                    << " at physical address: " << (void *)xrtBOAddress(buf->handle) << "\n";

                if (buf->copy_to_device_pending) {
                    debug(user_context) << "  buffer has a copy to device pending.\n";
                    sync_bo_to_device(user_context, buf, buffer->host, buf->size);
                    buf->copy_to_device_pending = false;
                }
            }
            ret = xrtRunSetArg(run_handle, num_args, buf->handle);
        } else {
            switch (arg_types[num_args].bytes()) {
            case 1:
                ret = xrtRunSetArg(run_handle, num_args, *(uint8_t *)args[num_args]);
                break;
            case 2:
                ret = xrtRunSetArg(run_handle, num_args, *(uint16_t *)args[num_args]);
                break;
            case 4:
                ret = xrtRunSetArg(run_handle, num_args, *(uint32_t *)args[num_args]);
                break;
            case 8:
                ret = xrtRunSetArg(run_handle, num_args, *(uint64_t *)args[num_args]);
                break;
            default:
                halide_debug_assert(user_context, false);
            }
        }

        if (ret != 0) {
            xrtRunClose(run_handle);
            error(user_context)
                << "XRT: halide_xrt_run: "
                << "failed to set arg[" << num_args << "] for kernel: " << entry_name
                << ", error: " << ret << "\n";
            return halide_error_code_generic_error;
        }

        num_args++;
    }

    debug(user_context) << "XRT: halide_xrt_run: starting kernel: " << entry_name << "\n";

    t_before = halide_current_time_ns(user_context);

    ret = xrtRunStart(run_handle);
    if (ret != 0) {
        xrtRunClose(run_handle);
        error(user_context)
            << "XRT: halide_xrt_run: "
            << "failed to start kernel: " << entry_name
            << ", error: " << ret << "\n";
        return halide_error_code_generic_error;
    }

    ret = xrtRunWait(run_handle);

    t_after = halide_current_time_ns(user_context);

    xrtRunClose(run_handle);

    if (ret != ERT_CMD_STATE_COMPLETED) {
        error(user_context)
            << "XRT: halide_xrt_run: "
            << "error waiting for kernel run completion, error: " << ret << "\n";
        return halide_error_code_generic_error;
    }

    print(user_context) << "XRT: '" << entry_name << "' execution took " << (t_after - t_before) << " ns\n";

    return halide_error_code_success;
}

WEAK const struct halide_device_interface_t *halide_xrt_device_interface() {
    return &xrt_device_interface;
}

namespace {

WEAK __attribute__((destructor)) void halide_xrt_cleanup() {
    halide_xrt_device_release(nullptr);
}

}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace XRT {

WEAK halide_device_interface_impl_t xrt_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_xrt_device_malloc,
    halide_xrt_device_free,
    halide_xrt_device_sync,
    halide_xrt_device_release,
    halide_xrt_copy_to_host,
    halide_xrt_copy_to_device,
    halide_xrt_device_and_host_malloc,
    halide_xrt_device_and_host_free,
    halide_xrt_buffer_copy,
    halide_xrt_device_crop,
    halide_xrt_device_slice,
    halide_xrt_device_release_crop,
    halide_xrt_wrap_native,
    halide_xrt_detach_native,
};

WEAK halide_device_interface_t xrt_device_interface = {
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
    &xrt_device_interface_impl};

}  // namespace XRT
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

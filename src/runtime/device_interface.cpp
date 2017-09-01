#include "HalideRuntime.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

namespace Halide { namespace Runtime { namespace Internal {

struct device_handle_wrapper {
    uint64_t device_handle;
    const halide_device_interface_t *interface;
};

// TODO: Coarser grained locking, also consider all things that need
// to be atomic with respect to each other. At present only
// halide_copy_to_host and halide_copy_to_device are atomic with
// respect to each other. halide_device_malloc and halide_device_free
// are also candidates, but to do so they likely need to be able to do
// a copy internaly as well.
WEAK halide_mutex device_copy_mutex;

WEAK int copy_to_host_already_locked(void *user_context, struct halide_buffer_t *buf) {
    if (!buf->device_dirty()) {
        return 0;  // my, that was easy
    }

    debug(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty is true\n";
    const halide_device_interface_t *interface = buf->device_interface;
    if (buf->host_dirty()) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty and host_dirty are true\n";
        return halide_error_code_copy_to_host_failed;
    }
    if (interface == NULL) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " interface is NULL\n";
        return halide_error_code_no_device_interface;
    }
    int result = interface->impl->copy_to_host(user_context, buf);
    if (result != 0) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " device copy_to_host returned an error\n";
        return halide_error_code_copy_to_host_failed;
    }
    buf->set_device_dirty(false);
    halide_msan_annotate_buffer_is_initialized(user_context, buf);

    return result;
}

}}} // namespace Halide::Runtime::Internal

namespace {

__attribute__((always_inline))
int debug_log_and_validate_buf(void *user_context, const halide_buffer_t *buf_arg,
                                const char *routine) {
    if (buf_arg == NULL) {
        return halide_error_buffer_is_null(user_context, routine);
    }

    const halide_buffer_t &buf(*buf_arg);
    debug(user_context) << routine << " validating input buffer: " << buf << "\n";

    bool device_interface_set = (buf.device_interface != NULL);
    bool device_set = (buf.device != 0);
    if (device_set && !device_interface_set) {
        return halide_error_no_device_interface(user_context);
    }
    if (device_interface_set && !device_set) {
        return halide_error_device_interface_no_device(user_context);
    }

    bool host_dirty = buf.host_dirty();
    bool device_dirty = buf.device_dirty();
    if (host_dirty && device_dirty) {
        return halide_error_host_and_device_dirty(user_context);
    }
    /* TODO: we could test:
     *     (device_set || !device_dirty)
     * and:
     *     (buf.host != NULL || !host_dirty)
     * but these conditions can occur when freeing a buffer.
     * It is perhaps prudent to mandate reseting the dirty bit when freeing
     * the host field and setting it to nullptr, I am not convinced all code
     * does that at present. The same could occur on the device side, though
     * it is much more unlikely as halide_device_free does clear device_dirty.
     * At present we're taking the side of caution and not adding these to the
     * assertion.
     */
    return 0;
}

}

extern "C" {

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
WEAK void halide_device_release(void *user_context, const halide_device_interface_t *device_interface) {
    device_interface->impl->device_release(user_context);
}

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
WEAK int halide_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    ScopedMutexLock lock(&device_copy_mutex);

    int result = debug_log_and_validate_buf(user_context, buf, "halide_copy_to_host");
    if (result != 0) {
        return result;
    }

    return copy_to_host_already_locked(user_context, buf);
}

/** Copy image data from host memory to device memory. This should not be
 * called directly; Halide handles copying to the device automatically. */
WEAK int halide_copy_to_device(void *user_context,
                               struct halide_buffer_t *buf,
                               const halide_device_interface_t *device_interface) {
    int result = 0;

    ScopedMutexLock lock(&device_copy_mutex);

    result = debug_log_and_validate_buf(user_context, buf, "halide_copy_to_device");
    if (result != 0) {
        return result;
    }

    if (device_interface == NULL) {
        debug(user_context) << "halide_copy_to_device " << buf << " interface is NULL\n";
        if (buf->device_interface == NULL) {
            return halide_error_no_device_interface(user_context);
        }
        device_interface = buf->device_interface;
    }

    if (buf->device && buf->device_interface != device_interface) {
        debug(user_context) << "halide_copy_to_device " << buf << " flipping buffer to new device\n";
        if (buf->device_interface != NULL && buf->device_dirty()) {
            halide_assert(user_context, !buf->host_dirty());
            result = copy_to_host_already_locked(user_context, buf);
            if (result != 0) {
                debug(user_context) << "halide_copy_to_device " << buf << " flipping buffer halide_copy_to_host failed\n";
                return result;
            }
        }
        result = halide_device_free(user_context, buf);
        if (result != 0) {
            debug(user_context) << "halide_copy_to_device " << buf << " flipping buffer halide_device_free failed\n";
            return result;
        }
        buf->set_host_dirty(true); // force copy back to new device below.
    }

    if (buf->device == 0) {
        result = halide_device_malloc(user_context, buf, device_interface);
        if (result != 0) {
            debug(user_context) << "halide_copy_to_device " << buf
                                << " halide_copy_to_device call to halide_device_malloc failed\n";
            return result;
        }
    }

    if (buf->host_dirty()) {
        debug(user_context) << "halide_copy_to_device " << buf << " host is dirty\n";
        if (buf->device_dirty()) {
            debug(user_context) << "halide_copy_to_device " << buf << " dev_dirty is true error\n";
            return halide_error_code_copy_to_device_failed;
        } else {
            result = device_interface->impl->copy_to_device(user_context, buf);
            if (result == 0) {
                buf->set_host_dirty(false);
            } else {
                debug(user_context) << "halide_copy_to_device "
                                    << buf << "device copy_to_device returned an error\n";
                return halide_error_code_copy_to_device_failed;
            }
        }
    }

    return 0;
}

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
WEAK int halide_device_sync(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_sync");
    if (result != 0) {
        return result;
    }
    const halide_device_interface_t *device_interface = buf->device_interface;

    if (device_interface == NULL) {
        return halide_error_no_device_interface(user_context);
    }
    result = device_interface->impl->device_sync(user_context, buf);
    if (result) {
        return halide_error_code_device_sync_failed;
    } else {
        return 0;
    }
}

/** Allocate device memory to back a halide_buffer_t. */
WEAK int halide_device_malloc(void *user_context, struct halide_buffer_t *buf,
                              const halide_device_interface_t *device_interface) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_malloc");
    if (result != 0) {
        return result;
    }
    debug(user_context) << "halide_device_malloc: target device interface " << device_interface << "\n";

    const halide_device_interface_t *current_interface = buf->device_interface;

    // halide_device_malloc does not support switching interfaces.
    if (current_interface != NULL && current_interface != device_interface) {
        error(user_context) << "halide_device_malloc doesn't support switching interfaces\n";
        return halide_error_code_device_malloc_failed;
    }

    // Ensure code is not freed prematurely.
    // TODO: Exception safety...
    device_interface->impl->use_module();
    result = device_interface->impl->device_malloc(user_context, buf);
    device_interface->impl->release_module();

    if (result) {
        return halide_error_code_device_malloc_failed;
    } else {
        return 0;
    }
}

/** Free any device memory associated with a halide_buffer_t. */
WEAK int halide_device_free(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_free");
    if (result != 0) {
        return result;
    }

    const halide_device_interface_t *device_interface = buf->device_interface;
    if (device_interface != NULL) {
        // Ensure interface is not freed prematurely.
        // TODO: Exception safety...
        device_interface->impl->use_module();
        result = device_interface->impl->device_free(user_context, buf);
        device_interface->impl->release_module();
        halide_assert(user_context, buf->device == 0);
        if (result) {
            return halide_error_code_device_free_failed;
        } else {
            return 0;
        }
    }
    buf->set_device_dirty(false);
    return 0;
}

WEAK int halide_weak_device_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_device_free(user_context, buf);
}

/** Free any device memory associated with a halide_buffer_t and ignore any
 * error. Used when freeing as a destructor on an error. */
WEAK void halide_device_free_as_destructor(void *user_context, void *obj) {
    struct halide_buffer_t *buf = (struct halide_buffer_t *)obj;
    halide_device_free(user_context, buf);
}

/** Allocate host and device memory to back a buffer_t. Ideally this
 * will be a zero copy setup, but the default implementation may
 * separately allocate the host memory using halide_malloc and the
 * device memory using halide_device_malloc. */
WEAK int halide_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf,
                                       const halide_device_interface_t *device_interface) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_and_host_malloc");
    if (result != 0) {
        return result;
    }
    debug(user_context) << "halide_device_and_host_malloc: target device interface " << device_interface << "\n";

    const halide_device_interface_t *current_interface = buf->device_interface;

    // halide_device_malloc does not support switching interfaces.
    if (current_interface != NULL && current_interface != device_interface) {
        halide_error(user_context, "halide_device_and_host_malloc doesn't support switching interfaces\n");
        return halide_error_code_device_malloc_failed;
    }

    // Ensure code is not freed prematurely.
    // TODO: Exception safety...
    device_interface->impl->use_module();
    result = device_interface->impl->device_and_host_malloc(user_context, buf);
    device_interface->impl->release_module();

    if (result != 0) {
        halide_error(user_context, "allocating host and device memory failed\n");
        return halide_error_code_device_malloc_failed;
    }
    return 0;
}

/** Free host and device memory associated with a buffer_t. */
WEAK int halide_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_and_host_free");
    if (result != 0) {
        return result;
    }

    const halide_device_interface_t *device_interface = buf->device_interface;
    if (device_interface != NULL) {
        // Ensure interface is not freed prematurely.
        // TODO: Exception safety...
        device_interface->impl->use_module();
        result = device_interface->impl->device_and_host_free(user_context, buf);
        device_interface->impl->release_module();
        halide_assert(user_context, buf->device == 0);
        if (result) {
            return halide_error_code_device_free_failed;
        } else {
            return 0;
        }
    } else if (buf->host) {
        // device_free must have been called on this buffer (which
        // must be legal for the device interface that was
        // used). We'd better still free the host pointer.
        halide_free(user_context, buf->host);
        buf->host = NULL;
    }
    buf->set_device_dirty(false);
    return 0;
}

WEAK int halide_default_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf,
                                               const halide_device_interface_t *device_interface) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_default_device_and_host_malloc");
    if (result != 0) {
        return result;
    }
    size_t size = buf->size_in_bytes();
    buf->host = (uint8_t *)halide_malloc(user_context, size);
    if (buf->host == NULL) {
        return -1;
    }
    result = halide_device_malloc(user_context, buf, device_interface);
    if (result != 0) {
        halide_free(user_context, buf->host);
        buf->host = NULL;
    }
    return result;
}

WEAK int halide_default_device_and_host_free(void *user_context, struct halide_buffer_t *buf,
                                             const halide_device_interface_t *device_interface) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_default_device_and_host_free");
    if (result != 0) {
        return result;
    }
    result = halide_device_free(user_context, buf);
    if (buf->host) {
        halide_free(user_context, buf->host);
        buf->host = NULL;
    }
    buf->set_host_dirty(false);
    buf->set_device_dirty(false);
    return result;
}


WEAK int halide_device_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t handle,
                                   const halide_device_interface_t *device_interface) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_wrap_native");
    if (result != 0) {
        return result;
    }
    const halide_device_interface_t *current_interface = buf->device_interface;

    if (current_interface != NULL && current_interface != device_interface) {
        error(user_context) << "halide_device_wrap_native doesn't support switching interfaces\n";
        return halide_error_code_device_wrap_native_failed;
    }

    device_interface->impl->use_module();
    buf->device_interface = device_interface;
    result = device_interface->impl->wrap_native(user_context, buf, handle);
    device_interface->impl->release_module();

    if (result) {
        return halide_error_code_device_malloc_failed;
    }
    return 0;
}

WEAK int halide_device_detach_native(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_detach_native");
    if (result != 0) {
        return result;
    }
    const halide_device_interface_t *device_interface = buf->device_interface;
    if (device_interface != NULL) {
        device_interface->impl->use_module();
        result = device_interface->impl->detach_native(user_context, buf);
        device_interface->impl->release_module();
        halide_assert(user_context, buf->device == 0);
        if (result) {
            result = halide_error_code_device_detach_native_failed;
        }
    }
    return result;
}

WEAK int halide_default_device_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t handle) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_default_device_wrap_native");
    if (result != 0) {
        return result;
    }
    buf->device_interface->impl->use_module();
    buf->device = handle;
    return 0;
}

WEAK int halide_default_device_detach_native(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_default_device_detach_native");
    if (result != 0) {
        return result;
    }
    if (buf->device == 0) {
        return 0;
    }
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return 0;
}

/** Free any host and device memory associated with a buffer_t and ignore any
 * error. Used when freeing as a destructor on an error. */
WEAK void halide_device_and_host_free_as_destructor(void *user_context, void *obj) {
    struct halide_buffer_t *buf = (struct halide_buffer_t *)obj;
    halide_device_and_host_free(user_context, buf);
}

/** TODO: Find a way to elide host free without this hack. */
WEAK void halide_device_host_nop_free(void *user_context, void *obj) {
}

} // extern "C" linkage

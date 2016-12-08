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
    const halide_device_interface *interface;
};

// TODO: Coarser grained locking, also consider all things that need
// to be atomic with respect to each other. At present only
// halide_copy_to_host and halide_copy_to_device are atomic with
// respect to each other. halide_device_malloc and halide_device_free
// are also candidates, but to do so they likely need to be able to do
// a copy internaly as well.
WEAK halide_mutex device_copy_mutex;

WEAK int copy_to_host_already_locked(void *user_context, struct halide_buffer_t *buf) {
    if (!buf->dev_dirty) {
        return 0;  // my, that was easy
    }

    debug(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty is true\n";
    const halide_device_interface_t *interface = buf->device_interface;
    if (buf->host_dirty()) {
        error(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty and host_dirty are true\n";
        result = halide_error_code_copy_to_host_failed;
    } else if (interface == NULL) {
        error(user_context) << "copy_to_host_already_locked " << buf << " interface is NULL\n";
        result = halide_error_code_no_device_interface;
    } else {
        result = interface->copy_to_host(user_context, buf);
        if (result == 0) {
            buf->set_device_dirty(false);
        } else {
            debug(user_context) << "copy_to_host_already_locked " << buf << " device copy_to_host returned an error\n";
            result = halide_error_code_copy_to_host_failed;
        }
    }
    halide_msan_annotate_buffer_is_initialized(user_context, buf);

    return result;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
WEAK void halide_device_release(void *user_context, const halide_device_interface_t *device_interface) {
    device_interface->device_release(user_context);
}

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
WEAK int halide_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    ScopedMutexLock lock(&device_copy_mutex);

    debug(NULL) << "halide_copy_to_host " << buf << "\n";

    return copy_to_host_already_locked(user_context, buf);
}

/** Copy image data from host memory to device memory. This should not be
 * called directly; Halide handles copying to the device automatically. */
WEAK int halide_copy_to_device(void *user_context,
                               struct halide_buffer_t *buf,
                               const halide_device_interface_t *device_interface) {
    int result = 0;

    ScopedMutexLock lock(&device_copy_mutex);


    debug(user_context)
        << "halide_copy_to_device " << buf
        << ", host: " << buf->host
        << ", dev: " << buf->device
        << ", host_dirty: " << buf->host_dirty()
        << ", dev_dirty: " << buf->device_dirty() << "\n";
    if (device_interface == NULL) {
        debug(user_context) << "halide_copy_to_device " << buf << " interface is NULL\n";
        if (buf->device_interface == NULL) {
            debug(user_context) << "halide_copy_to_device " << buf << " no interface error\n";
            return halide_error_code_no_device_interface;
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
            result = halide_error_code_copy_to_device_failed;
        } else {
            result = device_interface->copy_to_device(user_context, buf);
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
    const halide_device_interface_t *device_interface = NULL;
    if (buf) {
        device_interface = buf->device_interface;
    }
    if (device_interface == NULL) {
        debug(user_context) << "halide_device_sync on buffer with no interface\n";
        return halide_error_code_no_device_interface;
    }
    int result = device_interface->device_sync(user_context, buf);
    if (result) {
        return halide_error_code_device_sync_failed;
    } else {
        return 0;
    }
}

/** Allocate device memory to back a halide_buffer_t. */
WEAK int halide_device_malloc(void *user_context, struct halide_buffer_t *buf,
                              const halide_device_interface_t *device_interface) {
    const halide_device_interface_t *current_interface = buf->device_interface;
    debug(user_context) << "halide_device_malloc: " << buf
                        << " interface " << device_interface
                        << " host: " << buf->host
                        << ", dev: " << buf->device
                        << ", host_dirty: " << buf->host_dirty()
                        << ", dev_dirty:" << buf->device_dirty()
                        << " buf current interface: " << current_interface << "\n";

    // halide_device_malloc does not support switching interfaces.
    if (current_interface != NULL && current_interface != device_interface) {
        error(user_context) << "halide_device_malloc doesn't support switching interfaces\n";
        return halide_error_code_device_malloc_failed;
    }

    // Ensure code is not freed prematurely.
    // TODO: Exception safety...
    device_interface->use_module();
    int result = device_interface->device_malloc(user_context, buf);
    device_interface->release_module();

    if (result) {
        return halide_error_code_device_malloc_failed;
    } else {
        return 0;
    }
}

/** Free any device memory associated with a halide_buffer_t. */
WEAK int halide_device_free(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context) << "halide_device_free: " << buf
                        << " buf dev " << buf->device
                        << " interface " << buf->device_interface << "\n";
    if (buf != NULL) {
        const halide_device_interface_t *device_interface = buf->device_interface;
        if (device_interface != NULL) {
            // Ensure interface is not freed prematurely.
            // TODO: Exception safety...
            device_interface->use_module();
            int result = device_interface->device_free(user_context, buf);
            device_interface->release_module();
            halide_assert(user_context, buf->device == 0);
            if (result) {
                return halide_error_code_device_free_failed;
            } else {
                return 0;
            }
        }
    }
    buf->set_device_dirty(false);
    return 0;
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
    const halide_device_interface_t *current_interface = buf->device_interface;
    debug(user_context) << "halide_device_and_host_malloc: " << buf
                        << " interface " << device_interface
                        << " host: " << buf->host
                        << ", device: " << buf->device
                        << ", host_dirty: " << buf->host_dirty()
                        << ", dev_dirty:" << buf->device_dirty()
                        << " buf current interface: " << current_interface << "\n";

    // halide_device_malloc does not support switching interfaces.
    if (current_interface != NULL && current_interface != device_interface) {
        error(user_context) << "halide_device_and_host_malloc doesn't support switching interfaces\n";
        return halide_error_code_device_malloc_failed;
    }

    // Ensure code is not freed prematurely.
    // TODO: Exception safety...
    device_interface->use_module();
    int result = device_interface->device_and_host_malloc(user_context, buf);
    device_interface->release_module();

    if (result) {
        return halide_error_code_device_malloc_failed;
    } else {
        return 0;
    }
}

/** Free host and device memory associated with a buffer_t. */
WEAK int halide_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context) << "halide_device_and_host_free: " << buf
                        << " buf dev " << buf->device
                        << " interface " << buf->device_interface << "\n";
    if (buf != NULL) {
        const halide_device_interface_t *device_interface = buf->device_interface;
        if (device_interface != NULL) {
            // Ensure interface is not freed prematurely.
            // TODO: Exception safety...
            device_interface->use_module();
            int result = device_interface->device_and_host_free(user_context, buf);
            device_interface->release_module();
            halide_assert(user_context, buf->device == 0);
            if (result) {
                return halide_error_code_device_free_failed;
            } else {
                return 0;
            }
        }
    }
    buf->set_device_dirty(false);
    return 0;
}

WEAK int halide_default_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf,
                                               const halide_device_interface_t *device_interface) {
    size_t size = buf->size_in_bytes();
    buf->host = (uint8_t *)halide_malloc(user_context, size);
    if (buf->host == NULL) {
        return -1;
    }
    int result = halide_device_malloc(user_context, buf, device_interface);
    if (result != 0) {
        halide_free(user_context, buf->host);
        buf->host = NULL;
    }
    return result;
}

WEAK int halide_default_device_and_host_free(void *user_context, struct halide_buffer_t *buf,
                                             const halide_device_interface_t *device_interface) {
    int result = halide_device_free(user_context, buf);
    halide_free(user_context, buf->host);
    buf->host = NULL;
    buf->set_host_dirty(false);
    buf->set_device_dirty(false);
    return result;
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

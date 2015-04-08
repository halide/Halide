#include "runtime_internal.h"
#include "device_interface.h"
#include "HalideRuntime.h"
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

WEAK int copy_to_host_already_locked(void *user_context, struct buffer_t *buf) {
    int result = 0;

    if (halide_buffer_get_dev_dirty(buf)) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty is true\n";
        const halide_device_interface *interface = halide_get_device_interface(buf->dev);
        if (halide_buffer_get_host_dirty(buf)) {
            debug(user_context) << "copy_to_host_already_locked " << buf << " dev_dirty and host_dirty are true\n";
            result = halide_error_code_copy_to_host_failed;
        } else if (interface == NULL) {
            debug(user_context) << "copy_to_host_already_locked " << buf << " interface is NULL\n";
            result = halide_error_code_no_device_interface;
        } else {
            result = interface->copy_to_host(user_context, buf);
            if (result == 0) {
                halide_buffer_set_dev_dirty(buf, false);
            } else {
                debug(user_context) << "copy_to_host_already_locked " << buf << " device copy_to_host returned an error\n";
                result = halide_error_code_copy_to_host_failed;
            }
        }
    }

    return result;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK uint64_t halide_new_device_wrapper(uint64_t handle, const struct halide_device_interface *interface) {
    // Using malloc instead of halide_malloc avoids alignment overhead.
    device_handle_wrapper *wrapper = (device_handle_wrapper *)malloc(sizeof(device_handle_wrapper));
    if (wrapper == NULL) {
        return 0;
    }
    wrapper->device_handle = handle;
    wrapper->interface = interface;

    interface->use_module();

    debug(NULL) << "Creating device wrapper for interface " << interface << " handle " << (void *)handle << " wrapper " << wrapper << "\n";
    return (uint64_t)wrapper;
}

WEAK void halide_delete_device_wrapper(uint64_t wrapper) {
    device_handle_wrapper *wrapper_ptr = (device_handle_wrapper *)wrapper;
    wrapper_ptr->interface->release_module();
    debug(NULL) << "Deleting device wrapper for interface " << wrapper_ptr->interface << " device_handle " << (void *)wrapper_ptr->device_handle << " at addr " << wrapper_ptr << "\n";
    free(wrapper_ptr);
}

WEAK uint64_t halide_get_device_handle(uint64_t dev_field) {
    const device_handle_wrapper *wrapper = (const device_handle_wrapper *)dev_field;
    if (wrapper == NULL) {
        debug(NULL) << "Getting device handle for NULL wrapper\n";
        return 0;
    }
    debug(NULL) << "Getting device handle for interface " << wrapper->interface
                << " device_handle " << (void *)wrapper->device_handle
                << " at addr " << wrapper << "\n";
    return wrapper->device_handle;
}

WEAK const halide_device_interface *halide_get_device_interface(uint64_t dev_field) {
    if (dev_field == 0) {
        return NULL;
    }
    return ((device_handle_wrapper *)dev_field)->interface;
}

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
WEAK void halide_device_release(void *user_context, const halide_device_interface *interface) {
    interface->device_release(user_context);
}

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
WEAK int halide_copy_to_host(void *user_context, struct buffer_t *buf) {
    ScopedMutexLock lock(&device_copy_mutex);

    debug(NULL) << "halide_copy_to_host " << buf << "\n";

    return copy_to_host_already_locked(user_context, buf);
}

/** Copy image data from host memory to device memory. This should not be
 * called directly; Halide handles copying to the device automatically. */
WEAK int halide_copy_to_device(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    int result = 0;

    ScopedMutexLock lock(&device_copy_mutex);

    debug(user_context) << "halide_copy_to_device " << buf
                        << ", host: " << buf->host
                        << ", dev: " << buf->dev
                        << ", host_dirty: "
                        << halide_buffer_get_host_dirty(buf)
                        << ", dev_dirty:" << halide_buffer_get_dev_dirty(buf)
                        << "\n";
    const halide_device_interface *buf_dev_interface = halide_get_device_interface(buf->dev);
    if (interface == NULL) {
        debug(user_context) << "halide_copy_to_device " << buf << " interface is NULL\n";
        if (buf_dev_interface == NULL) {
            debug(user_context) << "halide_copy_to_device " << buf << " no interface error\n";
            return halide_error_code_no_device_interface;
        }
        interface = buf_dev_interface;
    }

    if (buf->dev && buf_dev_interface != interface) {
        debug(user_context) << "halide_copy_to_device " << buf << " flipping buffer to new device\n";
        if (buf_dev_interface != NULL && halide_buffer_get_dev_dirty(buf)) {
            halide_assert(user_context, !halide_buffer_get_host_dirty(buf));
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
        halide_buffer_set_host_dirty(buf, true);  // force copy back to new device below.
    }

    if (buf->dev == 0) {
        result = halide_device_malloc(user_context, buf, interface);
        if (result != 0) {
            debug(user_context) << "halide_copy_to_device " << buf
                                << " halide_copy_to_device call to halide_device_malloc failed\n";
            return result;
        }
    }

    if (halide_buffer_get_host_dirty(buf)) {
        debug(user_context) << "halide_copy_to_device " << buf << " host is dirty\n";
        if (halide_buffer_get_dev_dirty(buf)) {
            debug(user_context) << "halide_copy_to_device " << buf << " dev_dirty is true error\n";
            result = halide_error_code_copy_to_device_failed;
        } else {
            result = interface->copy_to_device(user_context, buf);
            if (result == 0) {
                halide_buffer_set_host_dirty(buf, false);
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
WEAK int halide_device_sync(void *user_context, struct buffer_t *buf) {
    const halide_device_interface *interface = NULL;
    if (buf) {
        interface = halide_get_device_interface(buf->dev);
    }
    if (interface == NULL) {
        debug(user_context) << "halide_device_sync on buffer with no interface\n";
        return halide_error_code_no_device_interface;
    }
    int result = interface->device_sync(user_context, buf);
    if (result) {
        return halide_error_code_device_sync_failed;
    } else {
        return 0;
    }
}

/** Allocate device memory to back a buffer_t. */
WEAK int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    const halide_device_interface *current_interface = halide_get_device_interface(buf->dev);
    debug(user_context) << "halide_device_malloc: " << buf
                        << " interface " << interface
                        << " host: " << buf->host
                        << ", dev: " << buf->dev
                        << ", host_dirty: " << halide_buffer_get_host_dirty(buf)
                        << ", dev_dirty:" << halide_buffer_get_dev_dirty(buf)
                        << " buf current interface: " << current_interface << "\n";

    // halide_device_malloc does not support switching interfaces.
    if (current_interface != NULL && current_interface != interface) {
        debug(user_context) << "halide_malloc doesn't support switching interfaces\n";
        return halide_error_code_device_malloc_failed;
    }

    // Ensure code is not freed prematurely.
    // TODO: Exception safety...
    interface->use_module();
    int result = interface->device_malloc(user_context, buf);
    interface->release_module();

    if (result) {
        return halide_error_code_device_malloc_failed;
    } else {
        return 0;
    }
}

/** Free any device memory associated with a buffer_t. */
WEAK int halide_device_free(void *user_context, struct buffer_t *buf) {
    uint64_t dev_field = 0;
    if (buf) {
        dev_field = buf->dev;
    }
    debug(user_context) << "halide_device_free: " << buf
                        << " buf dev " << buf->dev
                        << " interface " << halide_get_device_interface(dev_field) << "\n";
    if (buf != NULL) {
        const halide_device_interface *interface = halide_get_device_interface(dev_field);
        if (interface != NULL) {
            // Ensure interface is not freed prematurely.
            // TODO: Exception safety...
            interface->use_module();
            int result = interface->device_free(user_context, buf);
            interface->release_module();
            halide_assert(user_context, buf->dev == 0);
            if (result) {
                return halide_error_code_device_free_failed;
            } else {
                return 0;
            }
        }
    }
    halide_buffer_set_dev_dirty(buf, false);
    return 0;
}

/** Free any device memory associated with a buffer_t and ignore any
 * error. Used when freeing as a destructor. */
WEAK void halide_device_free_as_destructor(void *user_context, void *obj) {
    struct buffer_t *buf = (struct buffer_t *)obj;
    halide_device_free(user_context, buf);
}

} // extern "C" linkage

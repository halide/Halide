#include "runtime_internal.h"
#include "../buffer_t.h"
#include "HalideRuntime.h"
#include "device_interface.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);

}

namespace Halide { namespace Runtime { namespace Internal {

struct device_handle_wrapper {
    uint64_t device_handle;
    const halide_device_interface *interface;
};

WEAK uint64_t new_device_wrapper(uint64_t handle, const struct halide_device_interface *interface) {
    // Using malloc instead of halide_malloc avoids alignment overhead.
    device_handle_wrapper *wrapper = (device_handle_wrapper *)malloc(sizeof(device_handle_wrapper));
    if (wrapper == NULL) {
        return 0;
    }
    wrapper->device_handle = handle;
    wrapper->interface = interface;
    debug(NULL) << "Creating device wrapper for interface " << interface << " handle " << (void *)handle << " wrapper " << wrapper << "\n";
    return (uint64_t)wrapper;
}

WEAK void delete_device_wrapper(uint64_t wrapper) {
    device_handle_wrapper *wrapper_ptr = (device_handle_wrapper *)wrapper;
    debug(NULL) << "Deleting device wrapper for interface " << wrapper_ptr->interface << " device_handle " << (void *)wrapper_ptr->device_handle << " at addr " << wrapper_ptr << "\n";
    free(wrapper_ptr);
}

WEAK uint64_t get_device_handle(uint64_t dev_field) {
    const device_handle_wrapper *wrapper = (const device_handle_wrapper *)dev_field;
    if (wrapper == NULL) {
        debug(NULL) << "Getting device handle for NULL wrappe\n";
        return 0;
    }
    debug(NULL) << "Getting device handle for interface " << wrapper->interface << " device_handle " << (void *)wrapper->device_handle << " at addr " << wrapper << "\n";
    return wrapper->device_handle;
}

inline const halide_device_interface *get_device_interface(struct buffer_t *buf) {
    if (buf->dev == 0) {
        return NULL;
    }
    return ((device_handle_wrapper *)buf->dev)->interface;
}

}}} // namespace Halide::Runtime::Internal

extern "C" {

/** Release all data associated with the current GPU backend, in particular
 * all resources (memory, texture, context handles) allocated by Halide. Must
 * be called explicitly when using AOT compilation. */
void halide_device_release(void *user_context, const halide_device_interface *interface) {
    interface->device_release(user_context);
}

/** Copy image data from device memory to host memory. This must be called
 * explicitly to copy back the results of a GPU-based filter. */
int halide_copy_to_host(void *user_context, struct buffer_t *buf) {
    int result = 0;
    debug(NULL) << "halide_copy_to_host " << buf << "\n";
    if (buf->dev_dirty) {
        debug(NULL) << "halide_copy_to_host " << buf << "dev_dirty is true\n";
        const halide_device_interface *interface = get_device_interface(buf);
        if (buf->host_dirty) {
            debug(NULL) << "halide_copy_to_host " << buf << "dev_dirty and host_dirty are true\n";
            result = -1; // TODO: what value?
        } else if (interface == NULL) {
            debug(NULL) << "halide_copy_to_host " << buf << "interface is NULL\n";
            result = -2; // TODO: What value?
        } else {
            result = interface->copy_to_host(user_context, buf);
            if (result == 0) {
              buf->dev_dirty = false;
            } else {
              debug(NULL) << "halide_copy_to_host " << buf << "device copy_to_host returned an error\n";
            }
        }
    }
    return result;
}

/** Copy image data from host memory to device memory. This should not be
 * called directly; Halide handles copying to the device automatically. */
int halide_copy_to_device(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    int result = 0;
    debug(NULL) << "halide_copy_to_device " << buf << "\n";
    if (buf->host_dirty) {
        debug(NULL) << "halide_copy_to_device " << buf << "host_dirty is true\n";
        const halide_device_interface *buf_dev_interface = get_device_interface(buf);
        if (interface == NULL) { // TODO: Is this a good idea?
            debug(NULL) << "halide_copy_to_device " << buf << "interface is NULL\n";
            interface = buf_dev_interface;
        } else if (buf_dev_interface != NULL && buf_dev_interface != interface) {
            debug(NULL) << "halide_copy_to_device " << buf << "flipping buffer to new device\n";
            result = halide_copy_to_host(user_context, buf);
            if (result != 0) {
                debug(NULL) << "halide_copy_to_device " << buf << "flipping buffer halide_copy_to_host failed\n";
                return result;
            }
            result = halide_device_free(user_context, buf);
            if (result != 0) {
                debug(NULL) << "halide_copy_to_device " << buf << "flipping buffer halide_device_free failed\n";
                return result;
            }
            result = halide_device_malloc(user_context, buf, interface);
            if (result != 0) {
                debug(NULL) << "halide_copy_to_device " << buf << "flipping buffer halide_device_malloc failed\n";
                return result;
            }
        }

        if (buf->dev_dirty) {
            debug(NULL) << "halide_copy_to_device " << buf << "dev_dirty is true error\n";
            result = -1; // TODO: What value?
        } else if (interface == NULL) {
            debug(NULL) << "halide_copy_to_device " << buf << "no interface error\n";
            result = -2; // TODO: What value?
        }
        result = interface->copy_to_device(user_context, buf);
        if (result == 0) {
            buf->host_dirty = 0;
        } else {
            debug(NULL) << "halide_copy_to_device " << buf << "device copy_to_device returned an error\n";
        }
    }
    return result;
}

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
int halide_device_sync(void *user_context, struct buffer_t *buf) { 
    const halide_device_interface *interface = get_device_interface(buf);
    if (interface == NULL) {
        return -1;
    }
    return interface->device_sync(user_context, buf);
}

/** Allocate device memory to back a buffer_t. */
int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    debug(user_context) << "halide_device_malloc: " << buf << " buf dev " << buf->dev << " interface " << interface << "\n";
    int result = interface->device_malloc(user_context, buf);
    if (result == 0) {
      buf->host_dirty = true;
    }
    return result;
}

/** Free any device memory associated with a buffer_t. */
int halide_device_free(void *user_context, struct buffer_t *buf) {
    debug(user_context) << "halide_device_free: " << buf << " buf dev " << buf->dev << " interface " << get_device_interface(buf) << "\n";
    if (buf != NULL) {
        const halide_device_interface *interface = get_device_interface(buf);
        if (interface != NULL) {
            int result = interface->device_free(user_context, buf);
            halide_assert(user_context, buf->dev == 0);
            return result;
        }
        // TODO: Should it be an error to call halide_device_free on a buffer with no device interface? I don't think so...
    }
    buf->dev_dirty = false;
    return 0;
}

} // extern "C" linkage

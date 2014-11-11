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
    const halide_device_interface *interface = get_device_interface(buf);
    if (interface == NULL) {
        return 0;
    }
    return interface->copy_to_host(user_context, buf);
}

/** Copy image data from host memory to device memory. This should not be
 * called directly; Halide handles copying to the device automatically. */
int halide_copy_to_device(void *user_context, struct buffer_t *buf) {
    const halide_device_interface *interface = get_device_interface(buf);
    if (interface == NULL) {
        return 0;
    }
    return interface->copy_to_device(user_context, buf);
}

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
int halide_device_sync(void *user_context, struct buffer_t *buf) { 
    const halide_device_interface *interface = get_device_interface(buf);
    if (interface == NULL) {
        return 0;
    }
    return interface->device_sync(user_context, buf);
}

/** Allocate device memory to back a buffer_t. */
int halide_device_malloc(void *user_context, struct buffer_t *buf, const halide_device_interface *interface) {
    return interface->device_malloc(user_context, buf);
}

/** Free any device memory associated with a buffer_t. */
int halide_device_free(void *user_context, struct buffer_t *buf) {
    if (buf != NULL) {
        const halide_device_interface *interface = get_device_interface(buf);
        if (interface != NULL) {
            return interface->device_sync(user_context, buf);
        }
    }
    return 0;
}

} // extern "C" linkage

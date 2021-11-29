#include "device_interface.h"
#include "HalideRuntime.h"
#include "device_buffer_utils.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

extern "C" {

extern void *malloc(size_t);
extern void free(void *);
}

namespace Halide {
namespace Runtime {
namespace Internal {

struct device_handle_wrapper {
    uint64_t device_handle;
    const halide_device_interface_t *interface;
};

// TODO: Coarser grained locking, also consider all things that need
// to be atomic with respect to each other. At present only
// halide_copy_to_host, halide_copy_to_device, and halide_buffer_copy
// are atomic with respect to each other. halide_device_malloc and
// halide_device_free are also candidates, but to do so they likely
// need to be able to do a copy internaly as well.
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
    if (interface == nullptr) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " interface is nullptr\n";
        return halide_error_code_no_device_interface;
    }
    int result = interface->impl->copy_to_host(user_context, buf);
    if (result != 0) {
        debug(user_context) << "copy_to_host_already_locked " << buf << " device copy_to_host returned an error\n";
        return halide_error_code_copy_to_host_failed;
    }
    buf->set_device_dirty(false);
    (void)halide_msan_annotate_buffer_is_initialized(user_context, buf);  // ignore errors

    return result;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

namespace {

ALWAYS_INLINE int debug_log_and_validate_buf(void *user_context, const halide_buffer_t *buf_arg,
                                             const char *routine) {
    if (buf_arg == nullptr) {
        return halide_error_buffer_is_null(user_context, routine);
    }

    const halide_buffer_t &buf(*buf_arg);
    debug(user_context) << routine << " validating input buffer: " << buf << "\n";

    bool device_interface_set = (buf.device_interface != nullptr);
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
     *     (buf.host != nullptr || !host_dirty)
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

}  // namespace

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
WEAK int copy_to_device_already_locked(void *user_context,
                                       struct halide_buffer_t *buf,
                                       const halide_device_interface_t *device_interface) {
    int result = 0;

    result = debug_log_and_validate_buf(user_context, buf, "halide_copy_to_device");
    if (result != 0) {
        return result;
    }

    if (device_interface == nullptr) {
        debug(user_context) << "halide_copy_to_device " << buf << " interface is nullptr\n";
        if (buf->device_interface == nullptr) {
            return halide_error_no_device_interface(user_context);
        }
        device_interface = buf->device_interface;
    }

    if (buf->device && buf->device_interface != device_interface) {
        halide_error(user_context, "halide_copy_to_device does not support switching interfaces\n");
        return halide_error_code_incompatible_device_interface;
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
            debug(user_context) << "halide_copy_to_device " << buf << " calling copy_to_device()\n";
            result = device_interface->impl->copy_to_device(user_context, buf);
            if (result == 0) {
                buf->set_host_dirty(false);
            } else {
                debug(user_context) << "halide_copy_to_device "
                                    << buf << "device copy_to_device returned an error\n";
                return halide_error_code_copy_to_device_failed;
            }
        }
    } else {
        debug(user_context) << "halide_copy_to_device " << buf << " skipped (host is not dirty)\n";
    }

    return 0;
}

WEAK int halide_copy_to_device(void *user_context,
                               struct halide_buffer_t *buf,
                               const halide_device_interface_t *device_interface) {
    ScopedMutexLock lock(&device_copy_mutex);
    return copy_to_device_already_locked(user_context, buf, device_interface);
}

/** Wait for current GPU operations to complete. Calling this explicitly
 * should rarely be necessary, except maybe for profiling. */
WEAK int halide_device_sync(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_sync");
    if (result != 0) {
        return result;
    }
    const halide_device_interface_t *device_interface = buf->device_interface;

    if (device_interface == nullptr) {
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
    if (current_interface != nullptr && current_interface != device_interface) {
        halide_error(user_context, "halide_device_malloc doesn't support switching interfaces\n");
        return halide_error_code_incompatible_device_interface;
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
    if (device_interface != nullptr) {
        // Ensure interface is not freed prematurely.
        // TODO: Exception safety...
        device_interface->impl->use_module();
        result = device_interface->impl->device_free(user_context, buf);
        device_interface->impl->release_module();
        halide_abort_if_false(user_context, buf->device == 0);
        if (result) {
            return halide_error_code_device_free_failed;
        } else {
            return 0;
        }
    }
    buf->set_device_dirty(false);
    return 0;
}

/** Free any device memory associated with a halide_buffer_t and ignore any
 * error. Used when freeing as a destructor on an error. */
WEAK void halide_device_free_as_destructor(void *user_context, void *obj) {
    struct halide_buffer_t *buf = (struct halide_buffer_t *)obj;
    (void)halide_device_free(user_context, buf);  // ignore errors
}

/** Allocate host and device memory to back a halide_buffer_t. Ideally this
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
    if (current_interface != nullptr && current_interface != device_interface) {
        halide_error(user_context, "halide_device_and_host_malloc doesn't support switching interfaces\n");
        return halide_error_code_incompatible_device_interface;
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

/** Free host and device memory associated with a halide_buffer_t. */
WEAK int halide_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    int result = debug_log_and_validate_buf(user_context, buf, "halide_device_and_host_free");
    if (result != 0) {
        return result;
    }

    const halide_device_interface_t *device_interface = buf->device_interface;
    if (device_interface != nullptr) {
        // Ensure interface is not freed prematurely.
        // TODO: Exception safety...
        device_interface->impl->use_module();
        result = device_interface->impl->device_and_host_free(user_context, buf);
        device_interface->impl->release_module();
        halide_abort_if_false(user_context, buf->device == 0);
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
        buf->host = nullptr;
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
    if (buf->host == nullptr) {
        return -1;
    }
    result = halide_device_malloc(user_context, buf, device_interface);
    if (result != 0) {
        halide_free(user_context, buf->host);
        buf->host = nullptr;
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
        buf->host = nullptr;
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

    if (current_interface != nullptr && current_interface != device_interface) {
        halide_error(user_context, "halide_device_wrap_native doesn't support switching interfaces\n");
        return halide_error_code_incompatible_device_interface;
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
    if (device_interface != nullptr) {
        device_interface->impl->use_module();
        result = device_interface->impl->detach_native(user_context, buf);
        device_interface->impl->release_module();
        halide_abort_if_false(user_context, buf->device == 0);
        if (result) {
            result = halide_error_code_device_detach_native_failed;
        }
    }
    return result;
}

WEAK int halide_default_device_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t handle) {
    // No: this can return halide_error_no_device_interface for OGLC and HVX,
    // since `device_interface` may be set but `device` may not be. Instead,
    // just return halide_error_code_device_wrap_native_failed if buf->device isn't set.
    // (And *don't* halide_assert() here, as that will abort. Just return an error and let the caller handle it.)
    //
    // int result = debug_log_and_validate_buf(user_context, buf, "halide_default_device_wrap_native");
    // if (result != 0) {
    //     return result;
    // }
    if (buf->device != 0) {
        return halide_error_code_device_wrap_native_failed;
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
    buf->device_interface = nullptr;
    return 0;
}

/** Free any host and device memory associated with a halide_buffer_t and ignore any
 * error. Used when freeing as a destructor on an error. */
WEAK void halide_device_and_host_free_as_destructor(void *user_context, void *obj) {
    struct halide_buffer_t *buf = (struct halide_buffer_t *)obj;
    halide_device_and_host_free(user_context, buf);
}

/** TODO: Find a way to elide host free without this hack. */
WEAK void halide_device_host_nop_free(void *user_context, void *obj) {
}

WEAK int halide_default_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                    const struct halide_device_interface_t *dst_device_interface,
                                    struct halide_buffer_t *dst) {

    debug(user_context)
        << "halide_default_buffer_copy\n"
        << " source: " << *src << "\n"
        << " dst_device_interface: " << (void *)dst_device_interface << "\n"
        << " dst: " << *dst << "\n";

    // The right thing is that all devices have to support
    // device-to-device and device-to/from-arbitrarty-pointer.  This
    // means there will always have to be a device specifc version of
    // this function and the default can go away or fail. At present
    // there are some devices, e.g. OpenGL and OpenGLCompute, for which
    // this is not yet implemented.

    return halide_error_code_device_buffer_copy_failed;
}

WEAK int halide_buffer_copy_already_locked(void *user_context, struct halide_buffer_t *src,
                                           const struct halide_device_interface_t *dst_device_interface,
                                           struct halide_buffer_t *dst) {
    debug(user_context) << "halide_buffer_copy_already_locked called.\n";
    int err = 0;

    if (dst_device_interface && dst->device_interface &&
        dst_device_interface != dst->device_interface) {
        halide_error(user_context, "halide_buffer_copy does not support switching device interfaces");
        return halide_error_code_incompatible_device_interface;
    }

    if (dst_device_interface && !dst->device) {
        debug(user_context) << "halide_buffer_copy_already_locked: calling halide_device_malloc.\n";
        err = halide_device_malloc(user_context, dst, dst_device_interface);
        if (err) {
            return err;
        }
    }

    // First goal is correctness, the more interesting parts of which are:
    //      1) Respect dirty bits so data is valid.
    //      2) Don't infinitely recurse.
    // Second goal is efficiency:
    //      1) Try to do device-to-device if possible
    //      2) Minimum number of copies and minimum amount of copying otherwise.
    //      2a) e.g. for a device to different device buffer copy call where the copy must
    //          go through host memory, the src buffer may be left in device dirty state
    //          with the data copied through the destination host buffer to reduce the size
    //          of the copy.
    // The device specifc runtime routine may return an error for the
    // device to device case with separate devices. This code will attempt
    // to decompose the call via bouncing through host memory.
    //
    // At present some cases, such as different devices where there is
    // no host buffer, will return an error. Some of these could be
    // handled by allocating temporary host memory.
    //
    // It is assumed that if two device runtimes have copy compatible buffers
    // both will handle a copy between their types of buffers.

    // Give more descriptive names to conditions.
    const bool from_device_valid = (src->device != 0) &&
                                   (src->host == nullptr || !src->host_dirty());
    const bool to_device = dst_device_interface != nullptr;
    const bool to_host = dst_device_interface == nullptr;
    const bool from_host_exists = src->host != nullptr;
    const bool from_host_valid = from_host_exists &&
                                 (!src->device_dirty() || (src->device_interface == nullptr));
    const bool to_host_exists = dst->host != nullptr;

    if (to_host && !to_host_exists) {
        return halide_error_code_host_is_null;
    }

    // If a device to device copy is requested, try to do it directly.
    err = halide_error_code_incompatible_device_interface;
    if (from_device_valid && to_device) {
        debug(user_context) << "halide_buffer_copy_already_locked: device to device case.\n";
        err = dst_device_interface->impl->buffer_copy(user_context, src, dst_device_interface, dst);
    }

    if (err == halide_error_code_incompatible_device_interface) {
        // Return an error for a case that cannot make progress without a temporary allocation.
        // TODO: go ahead and do the temp allocation.
        if (!from_host_exists && !to_host_exists) {
            debug(user_context) << "halide_buffer_copy_already_locked: failing due to need for temp buffer.\n";
            return halide_error_code_incompatible_device_interface;
        }

        if (to_host && from_host_valid) {
            device_copy c = make_buffer_copy(src, true, dst, true);
            copy_memory(c, user_context);
            err = 0;
        } else if (to_host && from_device_valid) {
            debug(user_context) << "halide_buffer_copy_already_locked: to host case.\n";
            err = src->device_interface->impl->buffer_copy(user_context, src, nullptr, dst);
            // Return on success or an error indicating something other
            // than not handling this case went wrong.
            if (err == halide_error_code_incompatible_device_interface) {
                err = copy_to_host_already_locked(user_context, src);
                if (!err) {
                    err = halide_buffer_copy_already_locked(user_context, src, nullptr, dst);
                }
            }
        } else {
            if (from_device_valid && to_host_exists) {
                debug(user_context) << "halide_buffer_copy_already_locked: from_device_valid && to_host_exists case.\n";
                // dev -> dev via dst host memory
                debug(user_context) << " device -> device via dst host memory\n";
                err = src->device_interface->impl->buffer_copy(user_context, src, nullptr, dst);
                if (err == 0) {
                    dst->set_host_dirty(true);
                    err = copy_to_device_already_locked(user_context, dst, dst_device_interface);
                }
            } else if (to_device) {
                debug(user_context) << "halide_buffer_copy_already_locked: dev -> dev via src host memory.\n";
                // dev -> dev via src host memory.
                err = copy_to_host_already_locked(user_context, src);
                if (err == 0) {
                    err = dst_device_interface->impl->buffer_copy(user_context, src, dst_device_interface, dst);
                }
            } else {
                debug(user_context) << "halide_buffer_copy_already_locked: no valid copy mode found, failing.\n";
            }
        }
    }

    if (err != 0) {
        debug(user_context) << "halide_buffer_copy_already_locked: got error " << err << ".\n";
    }
    if (err == 0 && dst != src) {
        if (dst_device_interface) {
            debug(user_context) << "halide_buffer_copy_already_locked: setting device dirty.\n";
            dst->set_host_dirty(false);
            dst->set_device_dirty(true);
        } else {
            debug(user_context) << "halide_buffer_copy_already_locked: setting host dirty.\n";
            dst->set_host_dirty(true);
            dst->set_device_dirty(false);
        }
    }

    return err;
}

WEAK int halide_buffer_copy(void *user_context, struct halide_buffer_t *src,
                            const struct halide_device_interface_t *dst_device_interface,
                            struct halide_buffer_t *dst) {
    debug(user_context) << "halide_buffer_copy:\n"
                        << " src " << *src << "\n"
                        << " interface " << dst_device_interface << "\n"
                        << " dst " << *dst << "\n";

    ScopedMutexLock lock(&device_copy_mutex);

    if (dst_device_interface) {
        dst_device_interface->impl->use_module();
    }
    if (src->device_interface) {
        src->device_interface->impl->use_module();
    }

    int err = halide_buffer_copy_already_locked(user_context, src, dst_device_interface, dst);

    if (dst_device_interface) {
        dst_device_interface->impl->release_module();
    }
    if (src->device_interface) {
        src->device_interface->impl->release_module();
    }

    return err;
}

WEAK int halide_default_device_crop(void *user_context,
                                    const struct halide_buffer_t *src,
                                    struct halide_buffer_t *dst) {
    halide_error(user_context, "device_interface does not support cropping\n");
    return halide_error_code_device_crop_unsupported;
}

WEAK int halide_default_device_slice(void *user_context,
                                     const struct halide_buffer_t *src,
                                     int slice_dim, int slice_pos,
                                     struct halide_buffer_t *dst) {
    halide_error(user_context, "device_interface does not support slicing\n");
    return halide_error_code_device_crop_unsupported;
}

WEAK int halide_device_crop(void *user_context,
                            const struct halide_buffer_t *src,
                            struct halide_buffer_t *dst) {
    ScopedMutexLock lock(&device_copy_mutex);

    if (!src->device) {
        return 0;
    }

    if (dst->device) {
        halide_error(user_context, "destination buffer already has a device allocation\n");
        return halide_error_code_device_crop_failed;
    }

    if (src->dimensions != dst->dimensions) {
        halide_error(user_context, "src and dst must have identical dimensionality\n");
        return halide_error_code_device_crop_failed;
    }

    src->device_interface->impl->use_module();
    int err = src->device_interface->impl->device_crop(user_context, src, dst);

    debug(user_context) << "halide_device_crop "
                        << "\n"
                        << " src: " << *src << "\n"
                        << " dst: " << *dst << "\n";

    return err;
}

WEAK int halide_device_slice(void *user_context,
                             const struct halide_buffer_t *src,
                             int slice_dim, int slice_pos,
                             struct halide_buffer_t *dst) {
    ScopedMutexLock lock(&device_copy_mutex);

    if (!src->device) {
        return 0;
    }

    if (dst->device) {
        halide_error(user_context, "destination buffer already has a device allocation\n");
        return halide_error_code_device_crop_failed;
    }

    if (src->dimensions != dst->dimensions + 1) {
        halide_error(user_context, "dst must have exactly one fewer dimension than src\n");
        return halide_error_code_device_crop_failed;
    }

    src->device_interface->impl->use_module();
    int err = src->device_interface->impl->device_slice(user_context, src, slice_dim, slice_pos, dst);

    debug(user_context) << "halide_device_crop "
                        << "\n"
                        << " src: " << *src << "\n"
                        << " dst: " << *dst << "\n";

    return err;
}

WEAK int halide_default_device_release_crop(void *user_context,
                                            struct halide_buffer_t *buf) {
    if (!buf->device) {
        return 0;
    }
    halide_error(user_context, "device_interface does not support cropping\n");
    return halide_error_code_device_crop_unsupported;
}

WEAK int halide_device_release_crop(void *user_context,
                                    struct halide_buffer_t *buf) {
    if (buf->device) {
        ScopedMutexLock lock(&device_copy_mutex);
        const struct halide_device_interface_t *interface = buf->device_interface;
        int result = interface->impl->device_release_crop(user_context, buf);
        buf->device = 0;
        interface->impl->release_module();
        buf->device_interface = nullptr;
        return result;
    }
    return 0;
}

}  // extern "C" linkage

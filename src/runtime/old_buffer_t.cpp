#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace Halide { namespace Runtime { namespace Internal {
struct old_dev_wrapper {
    uint64_t device;
    const halide_device_interface_t *interface;
};

WEAK int guess_type_and_dimensionality(void *user_context, buffer_t *old_buf, halide_buffer_t *new_buf) {
    new_buf->dimensions = 4;
    for (int i = 0; i < 4; i++) {
        if (old_buf->extent[i] == 0) {
            new_buf->dimensions = i;
            break;
        }
    }

    switch (old_buf->elem_size) {
    case 1: new_buf->type = halide_type_of<uint8_t>(); break;
    case 2: new_buf->type = halide_type_of<uint16_t>(); break;
    case 4: new_buf->type = halide_type_of<uint32_t>(); break;
    case 8: new_buf->type = halide_type_of<uint64_t>(); break;
    default:
        return halide_error_failed_to_upgrade_buffer_t(user_context, "",
                                                       "elem_size of buffer was not in [1, 2, 4, 8]");
    }
    return 0;
}

}}}

extern "C" {

WEAK int halide_upgrade_buffer_t(void *user_context, const char *name,
                                 const buffer_t *old_buf, halide_buffer_t *new_buf,
                                 int bounds_query_only) {
    if (old_buf->host || old_buf->dev) {
        if (old_buf->elem_size != new_buf->type.bytes()) {
            // If we're not doing a bounds query, we expect the elem_size to match the type.
            stringstream sstr(user_context);
            sstr << "buffer has incorrect elem_size (" << old_buf->elem_size << ") "
                 << "for expected type (" << new_buf->type << ")";
            return halide_error_failed_to_upgrade_buffer_t(user_context, name, sstr.str());
        }
        if (bounds_query_only) {
            // Don't update.
            if (new_buf->host != old_buf->host) {
                // This should never happen, but if it does, we have a logic error in
                // wrapper generation: since we already have the upgrade overhead, let's
                // check and fail loudly rather than just have something weird happen.
                return halide_error_failed_to_upgrade_buffer_t(user_context, name,
                    "Internal error: buffer host mismatch in halide_upgrade_buffer_t.");
            }
            return 0;
        }
    }

    new_buf->host = old_buf->host;
    if (old_buf->dev) {
        old_dev_wrapper *wrapper = (old_dev_wrapper *)(old_buf->dev);
        new_buf->device = wrapper->device;
        new_buf->device_interface = wrapper->interface;
    } else {
        new_buf->device = 0;
        new_buf->device_interface = NULL;
    }
    for (int i = 0; i < new_buf->dimensions; i++) {
        new_buf->dim[i].min = old_buf->min[i];
        new_buf->dim[i].extent = old_buf->extent[i];
        new_buf->dim[i].stride = old_buf->stride[i];
    }
    new_buf->flags = 0;
    new_buf->set_host_dirty(old_buf->host_dirty);
    new_buf->set_device_dirty(old_buf->dev_dirty);
    return 0;
}

WEAK int halide_downgrade_buffer_t(void *user_context, const char *name,
                                   const halide_buffer_t *new_buf, buffer_t *old_buf) {
    memset(old_buf, 0, sizeof(buffer_t));
    if (new_buf->dimensions > 4) {
        return halide_error_failed_to_downgrade_buffer_t(user_context, name,
                                                         "buffer has more than four dimensions");
    }
    old_buf->host = new_buf->host;
    for (int i = 0; i < new_buf->dimensions; i++) {
        old_buf->min[i] = new_buf->dim[i].min;
        old_buf->extent[i] = new_buf->dim[i].extent;
        old_buf->stride[i] = new_buf->dim[i].stride;
    }
    old_buf->elem_size = new_buf->type.bytes();
    return halide_downgrade_buffer_t_device_fields(user_context, name, new_buf, old_buf);
}

WEAK int halide_downgrade_buffer_t_device_fields(void *user_context, const char *name,
                                                 const halide_buffer_t *new_buf, buffer_t *old_buf) {
    old_buf->host_dirty = new_buf->host_dirty();
    old_buf->dev_dirty = new_buf->device_dirty();
    if (new_buf->device) {
        if (old_buf->dev) {
            old_dev_wrapper *wrapper = (old_dev_wrapper *)old_buf->dev;
            wrapper->device = new_buf->device;
            wrapper->interface = new_buf->device_interface;
        } else {
            old_dev_wrapper *wrapper = (old_dev_wrapper *)malloc(sizeof(old_dev_wrapper));
            wrapper->device = new_buf->device;
            wrapper->interface = new_buf->device_interface;
            old_buf->dev = (uint64_t)wrapper;
        }
    } else if (old_buf->dev) {
        free((void *)old_buf->dev);
        old_buf->dev = 0;
    }
    return 0;
}

WEAK int halide_copy_to_host_legacy(void *user_context, struct buffer_t *old_buf) {
    halide_buffer_t new_buf = {0};
    halide_dimension_t shape[4];
    new_buf.dim = shape;
    int err = guess_type_and_dimensionality(user_context, old_buf, &new_buf);
    err = err || halide_upgrade_buffer_t(user_context, "", old_buf, &new_buf, /*bounds_query_only*/ 0);
    err = err || halide_copy_to_host(user_context, &new_buf);
    err = err || halide_downgrade_buffer_t_device_fields(user_context, "", &new_buf, old_buf);
    return err;
}

WEAK int halide_copy_to_device_legacy(void *user_context, struct buffer_t *old_buf,
                               const struct halide_device_interface_t *device_interface) {
    halide_buffer_t new_buf = {0};
    halide_dimension_t shape[4];
    new_buf.dim = shape;
    int err = guess_type_and_dimensionality(user_context, old_buf, &new_buf);
    err = err || halide_upgrade_buffer_t(user_context, "", old_buf, &new_buf, /*bounds_query_only*/ 0);
    err = err || halide_copy_to_device(user_context, &new_buf, device_interface);
    err = err || halide_downgrade_buffer_t_device_fields(user_context, "", &new_buf, old_buf);
    return err;
}

WEAK int halide_device_sync_legacy(void *user_context, struct buffer_t *old_buf) {
    halide_buffer_t new_buf = {0};
    halide_dimension_t shape[4];
    new_buf.dim = shape;
    int err = guess_type_and_dimensionality(user_context, old_buf, &new_buf);
    err = err || halide_upgrade_buffer_t(user_context, "", old_buf, &new_buf, /*bounds_query_only*/ 0);
    err = err || halide_device_sync(user_context, &new_buf);
    err = err || halide_downgrade_buffer_t_device_fields(user_context, "", &new_buf, old_buf);
    return err;
}

WEAK int halide_device_malloc_legacy(void *user_context, struct buffer_t *old_buf,
                              const struct halide_device_interface_t *device_interface) {
    halide_buffer_t new_buf = {0};
    halide_dimension_t shape[4];
    new_buf.dim = shape;
    int err = guess_type_and_dimensionality(user_context, old_buf, &new_buf);
    err = err || halide_upgrade_buffer_t(user_context, "", old_buf, &new_buf, /*bounds_query_only*/ 0);
    err = err || halide_device_malloc(user_context, &new_buf, device_interface);
    err = err || halide_downgrade_buffer_t_device_fields(user_context, "", &new_buf, old_buf);
    return err;
}

WEAK int halide_device_free_legacy(void *user_context, struct buffer_t *old_buf) {
    halide_buffer_t new_buf = {0};
    halide_dimension_t shape[4];
    new_buf.dim = shape;
    int err = guess_type_and_dimensionality(user_context, old_buf, &new_buf);
    err = err || halide_upgrade_buffer_t(user_context, "", old_buf, &new_buf, /*bounds_query_only*/ 0);
    err = err || halide_device_free(user_context, &new_buf);
    err = err || halide_downgrade_buffer_t_device_fields(user_context, "", &new_buf, old_buf);
    return err;
}

}  // extern "C"

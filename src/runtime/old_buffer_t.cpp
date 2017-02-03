#include "runtime_internal.h"
#include "HalideRuntime.h"
#include "printer.h"

namespace {
struct old_dev_wrapper {
    uint64_t device;
    const halide_device_interface_t *interface;
};
}

extern "C" {

WEAK int halide_upgrade_buffer_t(void *user_context, const char *name,
                                 const buffer_t *old_buf, halide_buffer_t *new_buf) {
    if ((old_buf->host || old_buf->dev) &&
        (old_buf->elem_size != new_buf->type.bytes())) {
        // Unless we're doing a bounds query, we expect the elem_size to match the type.
        stringstream sstr(user_context);
        sstr << "buffer has incorrect elem_size (" << old_buf->elem_size << ") "
             << "for expected type (" << new_buf->type << ")";
        return halide_error_failed_to_upgrade_buffer_t(user_context, name, sstr.str());
    }
    new_buf->host = old_buf->host;
    if (old_buf->dev) {
        old_dev_wrapper *wrapper = (old_dev_wrapper *)(old_buf->dev);
        new_buf->device = wrapper->device;
        new_buf->device_interface = wrapper->interface;
    }
    for (int i = 0; i < new_buf->dimensions; i++) {
        new_buf->dim[i].min = old_buf->min[i];
        new_buf->dim[i].extent = old_buf->extent[i];
        new_buf->dim[i].stride = old_buf->stride[i];
    }
    new_buf->flags = 0;
    new_buf->device = 0;
    new_buf->device_interface = NULL;
    new_buf->set_host_dirty(old_buf->host_dirty);
    new_buf->set_device_dirty(old_buf->dev_dirty);
    return 0;
}

WEAK int halide_downgrade_buffer_t(void *user_context, const char *name,
                                   const halide_buffer_t *new_buf, buffer_t *old_buf) {
    uint64_t old_dev = old_buf->dev;
    memset(old_buf, 0, sizeof(buffer_t));
    if (new_buf->dimensions > 4) {
        return halide_error_failed_to_downgrade_buffer_t(user_context, name,
                                                         "buffer has more than four dimensions");
    }
    old_buf->host = new_buf->host;
    if (new_buf->device) {
        old_dev_wrapper *wrapper = (old_dev_wrapper *)old_dev;
        if (!wrapper) {
            wrapper = (old_dev_wrapper *)malloc(sizeof(old_dev_wrapper));
        }
        wrapper->device = new_buf->device;
        wrapper->interface = new_buf->device_interface;
        old_buf->dev = (uint64_t)wrapper;
    }
    for (int i = 0; i < new_buf->dimensions; i++) {
        old_buf->min[i] = new_buf->dim[i].min;
        old_buf->extent[i] = new_buf->dim[i].extent;
        old_buf->stride[i] = new_buf->dim[i].stride;
    }
    old_buf->elem_size = new_buf->type.bytes();
    old_buf->host_dirty = new_buf->host_dirty();
    old_buf->dev_dirty = new_buf->device_dirty();
    return 0;
}

}

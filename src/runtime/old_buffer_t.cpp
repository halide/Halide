#include "runtime_internal.h"
#include "HalideRuntime.h"

extern "C" {

WEAK int halide_upgrade_buffer_t(void *user_context, const char *name,
                                 const buffer_t *old_buf, halide_buffer_t *new_buf) {
    if (old_buf->dev) {
        return halide_error_failed_to_upgrade_buffer_t(user_context, name,
                                                       "buffer has a device allocation");
    }
    new_buf->host = old_buf->host;
    for (int i = 0; i < new_buf->dimensions; i++) {
        new_buf->dim[i].min = old_buf->min[i];
        new_buf->dim[i].extent = old_buf->extent[i];
        new_buf->dim[i].stride = old_buf->stride[i];
    }
    return 0;
}

WEAK int halide_downgrade_buffer_t(void *user_context, const char *name,
                                   const halide_buffer_t *new_buf, buffer_t *old_buf) {
    if (new_buf->device) {
        return halide_error_failed_to_downgrade_buffer_t(user_context, name,
                                                         "buffer has a device allocation");
    }
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
    for (int i = new_buf->dimensions; i < 4; i++) {
        old_buf->min[i] = 0;
        old_buf->extent[i] = 0;
        old_buf->stride[i] = 0;
    }
    old_buf->elem_size = new_buf->type.bytes();
    return 0;
}

}

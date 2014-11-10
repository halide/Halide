#ifndef HALIDE_DEVICE_INTERFACE_H
#define HALIDE_DEVICE_INTERFACE_H

extern "C" {

struct halide_device_interface {
    int (*device_malloc)(void *user_context, struct buffer_t *buf);
    int (*device_free)(void *user_context, struct buffer_t *buf);
    int (*device_sync)(void *user_context, struct buffer_t *buf);
    int (*device_release)(void *user_context);
    int (*copy_to_host)(void *user_context, struct buffer_t *buf);
    int (*copy_to_device)(void *user_context, struct buffer_t *buf);
};

}

namespace Halide { namespace Runtime { namespace Internal {

extern WEAK uint64_t new_device_wrapper(uint64_t handle, const struct halide_device_interface *interface);
extern WEAK void delete_device_wrapper(uint64_t dev_field);
extern WEAK uint64_t get_device_handle(uint64_t dev_field);

}}} // namespace Halide::Runtime::Internal

#endif // HALIDE_DEVICE_INTERFACE_H

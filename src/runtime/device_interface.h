#ifndef HALIDE_DEVICE_INTERFACE_H
#define HALIDE_DEVICE_INTERFACE_H

extern "C" {

extern WEAK int halide_default_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf,
                                                      const struct halide_device_interface_t *device_interface);
extern WEAK int halide_default_device_and_host_free(void *user_context, struct halide_buffer_t *buf,
                                                    const struct halide_device_interface_t *device_interface);

}

#endif // HALIDE_DEVICE_INTERFACE_H

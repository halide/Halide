#include "runtime_internal.h"
#include "scoped_spin_lock.h"
#include "device_interface.h"
#include "../buffer_t.h"
#include "HalideRuntimeMetal.h"

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {

extern WEAK halide_device_interface metal_device_interface;

}}}} // namespace Halide::Runtime::Internal::Metal

using namespace Halide::Runtime::Internal::Metal;

extern "C" {

WEAK int halide_metal_device_malloc(void *user_context, buffer_t* buf) {
    return -1;
}


WEAK int halide_metal_device_free(void *user_context, buffer_t* buf) {
    return -1;
}

WEAK int halide_metal_initialize_kernels(void *user_context, void **state_ptr, const char* src, int size) {
    return -1;
}

WEAK int halide_metal_device_sync(void *user_context, struct buffer_t *) {
    return -1;
}

WEAK int halide_metal_device_release(void *user_context) {
    return -1;
}

WEAK int halide_metal_copy_to_device(void *user_context, buffer_t* buf) {
    return -1;
}

WEAK int halide_metal_copy_to_host(void *user_context, buffer_t* buf) {
    return -1;
}

WEAK int halide_metal_run(void *user_context,
                           void *state_ptr,
                           const char* entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void* args[],
                           int8_t arg_is_buffer[],
                           int num_attributes,
                           float* vertex_buffer,
                           int num_coords_dim0,
                           int num_coords_dim1) {
    return -1;
}

#if 0 // TODO: get naming right
WEAK int halide_metal_wrap_cl_mem(void *user_context, struct buffer_t *buf, uintptr_t mem) {
}

WEAK uintptr_t halide_metal_detach_cl_mem(void *user_context, struct buffer_t *buf) {
}

WEAK uintptr_t halide_metal_get_cl_mem(void *user_context, struct buffer_t *buf) {
}
#endif

WEAK const struct halide_device_interface *halide_metal_device_interface() {
    return &metal_device_interface;
}

namespace {
__attribute__((destructor))
WEAK void halide_metal_cleanup() {
    halide_metal_device_release(NULL);
}
}

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace Metal {
WEAK halide_device_interface metal_device_interface = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_metal_device_malloc,
    halide_metal_device_free,
    halide_metal_device_sync,
    halide_metal_device_release,
    halide_metal_copy_to_host,
    halide_metal_copy_to_device,
};

}}}} // namespace Halide::Runtime::Internal::Metal

#include "HalideRuntimeWebGPU.h"
#include "device_interface.h"
#include "printer.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace WebGPU {

extern WEAK halide_device_interface_t webgpu_device_interface;

}  // namespace WebGPU
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::WebGPU;

extern "C" {

WEAK int halide_webgpu_device_malloc(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_free(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_sync(void *user_context, halide_buffer_t *) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_release(void *user_context) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_copy_to_host(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_copy_to_device(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                   const struct halide_device_interface_t *dst_device_interface,
                                   struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_crop(void *user_context,
                                   const struct halide_buffer_t *src,
                                   struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_slice(void *user_context,
                                    const struct halide_buffer_t *src,
                                    int slice_dim,
                                    int slice_pos,
                                    struct halide_buffer_t *dst) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_device_release_crop(void *user_context,
                                           struct halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_wrap_native(void *user_context, struct halide_buffer_t *buf, uint64_t mem) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_detach_native(void *user_context, halide_buffer_t *buf) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK int halide_webgpu_initialize_kernels(void *user_context, void **state_ptr, const char *src, int size) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK void halide_webgpu_finalize_kernels(void *user_context, void *state_ptr) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
}

WEAK int halide_webgpu_run(void *user_context,
                           void *state_ptr,
                           const char *entry_name,
                           int blocksX, int blocksY, int blocksZ,
                           int threadsX, int threadsY, int threadsZ,
                           int shared_mem_bytes,
                           size_t arg_sizes[],
                           void *args[],
                           int8_t arg_is_buffer[]) {
    // TODO: Implement this.
    halide_debug_assert(user_context, false && "unimplemented");
    return 1;
}

WEAK const struct halide_device_interface_t *halide_webgpu_device_interface() {
    return &webgpu_device_interface;
}

namespace {
WEAK __attribute__((destructor)) void halide_webgpu_cleanup() {
    halide_webgpu_device_release(nullptr);
}
}  // namespace

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace WebGPU {

WEAK halide_device_interface_impl_t webgpu_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_webgpu_device_malloc,
    halide_webgpu_device_free,
    halide_webgpu_device_sync,
    halide_webgpu_device_release,
    halide_webgpu_copy_to_host,
    halide_webgpu_copy_to_device,
    halide_webgpu_device_and_host_malloc,
    halide_webgpu_device_and_host_free,
    halide_webgpu_buffer_copy,
    halide_webgpu_device_crop,
    halide_webgpu_device_slice,
    halide_webgpu_device_release_crop,
    halide_webgpu_wrap_native,
    halide_webgpu_detach_native,
};

WEAK halide_device_interface_t webgpu_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,
    &webgpu_device_interface_impl};

}  // namespace WebGPU
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

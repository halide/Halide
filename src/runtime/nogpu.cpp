#include "runtime_internal.h"
#include "HalideRuntime.h"  // has buffer_t

// Architectures that do not distinguish between device and host
// (i.e. not gpus), don't need a definition of copy_to_host

extern "C" WEAK int halide_copy_to_host(void *user_context, buffer_t *buf) {
    return 0;
}

extern "C" WEAK int halide_copy_to_dev(void *user_context, buffer_t *buf) {
    halide_error(user_context, "No gpu target enabled");
    return -1;
}

extern "C" WEAK int halide_dev_malloc(void *user_context, buffer_t *buf) {
    halide_error(user_context, "No gpu target enabled");
    return -1;
}

extern "C" WEAK int halide_dev_free(void *user_context, buffer_t *buf) {
    return 0;
}

extern "C" WEAK void halide_release(void *user_context) {
}

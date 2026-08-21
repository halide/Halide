#include "HalideRuntime.h"

extern "C" {

typedef uint32_t zx_handle_t;

extern zx_handle_t zx_thread_self();

WEAK int32_t halide_current_thread_id() {
    const zx_handle_t id = zx_thread_self();
    return (int32_t)(id ? id : 1);
}
}

#include "HalideRuntime.h"

/** \file JIT module reference counting support. */

/* The runtime can manipulate a reference count on its code because
 * some allocated data structures returned from the runtime may
 * contain function pointers back to this code. For JIT, the module
 * instantiation logic sets values into the globals below to allow
 * tracking which JITModule the code is in. (The goal being to
 * decouple the runtime from having to know details of the Halide JIT
 * support.)
 *
 * The reference count is increased when a new device allocation is
 * made through the device interface part of the runtime and decreased
 * when such an allocation is freed. The mechanism could be used in
 * other palces however.
 */

extern "C" {

WEAK void *halide_jit_module_argument = NULL;
WEAK void (*halide_jit_module_adjust_ref_count)(void *arg, int32_t count) = NULL;
}

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK void halide_use_jit_module() {
    if (halide_jit_module_adjust_ref_count == NULL) {
        return;
    } else {
        (*halide_jit_module_adjust_ref_count)(halide_jit_module_argument, 1);
    }
}

WEAK void halide_release_jit_module() {
    if (halide_jit_module_adjust_ref_count == NULL) {
        return;
    } else {
        (*halide_jit_module_adjust_ref_count)(halide_jit_module_argument, -1);
    }
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

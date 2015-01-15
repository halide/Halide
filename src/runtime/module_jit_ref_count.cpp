#include "./runtime_internal.h"

extern "C" {

WEAK void *halide_jit_module_argument = NULL;
WEAK void (*halide_jit_module_adjust_ref_count)(void *arg, int32_t count) = NULL;

}

namespace Halide { namespace Runtime { namespace Internal {

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

}}}


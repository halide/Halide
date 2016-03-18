#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

extern void halide_print_impl(void *, const char *);

WEAK halide_print_t custom_print = halide_print_impl;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_print(void *user_context, const char *msg) {
    (*custom_print)(user_context, msg);
}

WEAK halide_print_t halide_set_custom_print(halide_print_t print) {
    halide_print_t result = custom_print;
    custom_print = print;
    return result;
}

}

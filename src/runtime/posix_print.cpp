#include "HalideRuntime.h"

extern "C" void halide_default_print(void *, const char *);

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_print_t custom_print = halide_default_print;

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

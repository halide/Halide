#include "runtime_internal.h"

#include "HalideRuntime.h"

// For OSes that can't reference external symbols, we provide a print
// implementation that requires a custom print to be set.

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_print_t custom_print = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

// Print
WEAK halide_print_t halide_set_custom_print(halide_print_t print) {
    halide_print_t result = custom_print;
    custom_print = print;
    return result;
}

WEAK void halide_print(void *user_context, const char *msg) {
    (*custom_print)(user_context, msg);
}

}  // extern "C"

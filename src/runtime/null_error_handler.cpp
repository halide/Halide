#include "runtime_internal.h"

#include "HalideRuntime.h"

// For OSes that can't reference external symbols, we provide an error
// handler implementation that requires a custom error handler to be
// set.

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_error_handler_t error_handler = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

// Print
WEAK halide_print_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_print_t result = error_handler;
    error_handler = handler;
    return result;
}

WEAK void halide_error(void *user_context, const char *msg) {
    (*error_handler)(user_context, msg);
}

}  // extern "C"

#include "runtime_internal.h"
#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

extern void halide_print_impl(void *, const char *);
WEAK void (*halide_custom_print)(void *, const char *) = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_print(void *user_context, const char *msg) {
    if (halide_custom_print) {
        (*halide_custom_print)(user_context, msg);
    }  else {
        halide_print_impl(user_context, msg);
    }
}

WEAK void halide_set_custom_print(void (*print)(void *, const char *)) {
    halide_custom_print = print;
}

}

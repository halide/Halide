#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void (*halide_error_handler)(void *, const char *) = NULL;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_error(void *user_context, const char *msg) {
    if (halide_error_handler) {
        (*halide_error_handler)(user_context, msg);
    } else {
        char buf[4096];
        char *dst = halide_string_to_string(buf, buf + 4096, "Error: ");
        halide_string_to_string(dst, buf + 4096, msg);
        halide_print(user_context, buf);
        exit(1);
    }
}

WEAK void halide_set_error_handler(void (*handler)(void *, const char *)) {
    halide_error_handler = handler;
}

}

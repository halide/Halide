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
        char *dst = halide_string_to_string(buf, buf + 4095, "Error: ");
        dst = halide_string_to_string(dst, buf + 4095, msg);
        // We still have one character free. Add a newline if there
        // isn't one already.
        if (dst[-1] != '\n') {
            dst[0] = '\n';
            dst[1] = 0;
        }
        halide_print(user_context, buf);
        exit(1);
    }
}

WEAK void halide_set_error_handler(void (*handler)(void *, const char *)) {
    halide_error_handler = handler;
}

}

#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void default_error_handler(void *user_context, const char *msg) {
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

WEAK void (*halide_error_handler)(void *, const char *) = default_error_handler;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_error(void *user_context, const char *msg) {
    (*halide_error_handler)(user_context, msg);
}

WEAK void (*halide_set_error_handler(void (*handler)(void *, const char *)))(void *, const char *) {
    void (*result)(void *, const char *) = halide_error_handler;
    halide_error_handler = handler;
    return result;
}

}

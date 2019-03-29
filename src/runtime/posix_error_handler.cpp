#include "HalideRuntime.h"
#include "printer.h"
#include "runtime_internal.h"

extern "C" {

WEAK void halide_default_error(void *user_context, const char *msg) {
    char buf[4096];
    char *dst = halide_string_to_string(buf, buf + 4094, "Error: ");
    dst = halide_string_to_string(dst, dst + 4094, msg);
    // We still have one character free. Add a newline if there
    // isn't one already.
    if (dst[-1] != '\n') {
        dst[0] = '\n';
        dst[1] = 0;
        dst += 1;
    }
    halide_msan_annotate_memory_is_initialized(user_context, buf, dst - buf + 1);
    halide_print(user_context, buf);
    Halide::Runtime::Internal::halide_abort();
}

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK halide_error_handler_t error_handler = halide_default_error;

}}} // namespace Halide::Runtime::Internal

extern "C" {

WEAK void halide_error(void *user_context, const char *msg) {
    (*error_handler)(user_context, msg);
}

WEAK halide_error_handler_t halide_set_error_handler(halide_error_handler_t handler) {
    halide_error_handler_t result = error_handler;
    error_handler = handler;
    return result;
}

}

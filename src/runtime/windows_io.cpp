#include "runtime_internal.h"

extern "C" {

// MSVC doesn't have much of c99
WEAK int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args,fmt);
    int ret = vsnprintf(str, size, fmt, args);
    va_end(args);
    return ret;
}
}

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    write(STDOUT_FILENO, str, strlen(str));
}

}}} // namespace Halide::Runtime::Internal

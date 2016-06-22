#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    write(STDERR_FILENO, str, strlen(str));
}

}}} // namespace Halide::Runtime::Internal

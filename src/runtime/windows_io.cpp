#include "runtime_internal.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    write(STDOUT_FILENO, str, strlen(str));
}

}}} // namespace Halide::Runtime::Internal

#include "runtime_internal.h"

extern "C" {

extern int fprintf(void *stream, const char *format, ...);
extern void *stderr;

}

namespace Halide { namespace Runtime { namespace Internal {

WEAK void halide_print_impl(void *user_context, const char *str) {
    fprintf(stderr, "%s", str);
}

}}} // namespace Halide::Runtime::Internal

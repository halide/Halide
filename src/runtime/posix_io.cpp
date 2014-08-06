#include "runtime_internal.h"

extern "C" {

extern int fprintf(void *stream, const char *format, ...);
extern void *stderr;

}

namespace halide_runtime_internal {
WEAK void halide_print_impl(void *user_context, const char *str) {
    fprintf(stderr, "%s", str);
}
}

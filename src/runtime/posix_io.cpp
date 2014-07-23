#include "runtime_internal.h"

extern "C" {

extern int fprintf(void *stream, const char *format, ...);
extern void *stderr;

WEAK void __halide_print(void *user_context, const char *str) {
    fprintf(stderr, "%s", str);
}

}

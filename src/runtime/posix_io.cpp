#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int vfprintf(void *stream, const char *format, __builtin_va_list ap);
extern void *stderr;

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int ret = vfprintf(stderr, fmt, args);
    __builtin_va_end(args);
    return ret;
}

}

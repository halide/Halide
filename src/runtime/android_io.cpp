#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int __android_log_vprint(int, const char *, const char *, __builtin_va_list);

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int result = __android_log_vprint(7, "halide", fmt, args);
    __builtin_va_end(args);
    return result;
}

}

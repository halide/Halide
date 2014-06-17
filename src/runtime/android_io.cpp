#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int __android_log_vprint(int, const char *, const char *, __builtin_va_list);

#define ANDROID_LOG_INFO 4

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,fmt);
    int result = __android_log_vprint(ANDROID_LOG_INFO, "halide", fmt, args);
    __builtin_va_end(args);
    return result;
}

}

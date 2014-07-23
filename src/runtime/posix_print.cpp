#include "runtime_internal.h"
#include "HalideRuntime.h"
#include <stdarg.h>

extern "C" {
extern int vsnprintf (char *s, size_t n, const char *format, va_list arg);
extern void __halide_print(void *, const char *);

WEAK void (*halide_custom_print)(void *, const char *) = NULL;

WEAK void halide_print(void *user_context, const char *msg) {
    if (halide_custom_print) {
        (*halide_custom_print)(user_context, msg);
    }  else {
        __halide_print(user_context, msg);
    }
}

WEAK void halide_set_custom_print(void (*print)(void *, const char *)) {
    halide_custom_print = print;
}

WEAK int halide_printf(void *user_context, const char * fmt, ...) {
    char buffer[4096];
    va_list args;
    va_start(args,fmt);
    int ret = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    halide_print(user_context, buffer);
    return ret;
}

}

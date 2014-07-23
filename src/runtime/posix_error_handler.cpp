#include "runtime_internal.h"
#include <stdarg.h>

extern "C" {

extern int halide_printf(void *, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);
extern void exit(int);

WEAK void (*halide_error_handler)(void *, const char *) = NULL;

WEAK void halide_error(void *user_context, const char *msg) {
    if (halide_error_handler) {
        (*halide_error_handler)(user_context, msg);
    }  else {
        halide_printf(user_context, "Error: %s\n", msg);
        exit(1);
    }
}

WEAK void halide_error_varargs(void *user_context, const char *msg, ...) {
    char buf[4096];
    va_list args;
    va_start(args, msg);
    vsnprintf(buf, 4096, msg, args);
    va_end(args);
    halide_error(user_context, buf);
}

WEAK void halide_set_error_handler(void (*handler)(void *, const char *)) {
    halide_error_handler = handler;
}

}

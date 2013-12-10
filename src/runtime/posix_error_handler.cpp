#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int halide_printf(void *, const char *, ...);
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

WEAK void halide_set_error_handler(void (*handler)(void *, const char *)) {
    halide_error_handler = handler;
}

}

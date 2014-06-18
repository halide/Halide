#include "mini_stdint.h"

#define WEAK __attribute__((weak))

extern "C" {

extern int fprintf(void *stream, const char *format, ...);
extern void *stderr;

WEAK void __halide_print(void *user_context, const char *str) {
    fprintf(stderr, "%s", str);
}

}

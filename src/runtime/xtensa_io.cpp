#include "HalideRuntime.h"

extern "C" {

extern int printf(const char *format, ...);

WEAK void halide_default_print(void *user_context, const char *str) {
    printf("%s", str);
}

}

#include "HalideRuntime.h"

extern "C" {

WEAK void halide_default_print(void *user_context, const char *str) {
    write(STDOUT_FILENO, str, strlen(str));
}
}

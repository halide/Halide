#include "stdio.h"

extern "C" {

void halide_print(void *user_context, const char *str) {
    printf("%s", str);
}

void halide_error(void *user_context, const char *msg) {
    halide_print(user_context, msg);
}

}  // extern "C"

#include "stdio.h"

extern "C" {

void halide_print(void *user_context, const char *str) {
    printf("%s", str);
}

void halide_error(void *user_context, const char *msg) {
    halide_print(user_context, msg);
}

void halide_profiler_report(void *user_context) {
}

void halide_profiler_reset() {
}

}  // extern "C"

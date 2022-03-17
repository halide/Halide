#include <cstdint>

#include "HalideRuntime.h"
#include "runtime_internal.h"
#include "to_string.cpp"
#include "msan_stubs.cpp"

extern "C" {

extern int printf(const char * format, ... );

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

#include "printer.h"

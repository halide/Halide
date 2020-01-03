#include "HalideRuntime.h"

extern "C" {

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations.
WEAK __attribute__((always_inline)) int _halide_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 1, 3);
    return 0;
}
}

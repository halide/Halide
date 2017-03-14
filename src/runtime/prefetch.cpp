#include "HalideRuntime.h"

extern "C" {

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations.
__attribute__((always_inline))
WEAK int halide_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 1);
    return 0;
}

}

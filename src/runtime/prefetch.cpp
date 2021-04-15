#include "HalideRuntime.h"

extern "C" {

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations, but needs to be WEAK
// so that Codegen_LLVM can find an instance of the Function to insert.
WEAK_INLINE int _halide_prefetch(const void *ptr) {
    __builtin_prefetch(ptr, 1, 3);
    return 0;
}
}

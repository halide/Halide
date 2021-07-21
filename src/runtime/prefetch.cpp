#include "HalideRuntime.h"

extern "C" {

// These need to inline, otherwise the extern call with the ptr
// parameter breaks a lot of optimizations, but needs to be WEAK
// so that Codegen_LLVM can find an instance of the Function to insert.
WEAK_INLINE int _halide_prefetch(const void *ptr) {
    constexpr int rw = 1;        // 1 = write, 0 = read
    constexpr int locality = 3;  // 0 = no temporal locality, 3 = high temporal locality
    __builtin_prefetch(ptr, rw, locality);
    return 0;
}
}

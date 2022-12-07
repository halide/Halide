#include "runtime_internal.h"

extern "C" {

WEAK_INLINE int halide_internal_malloc_alignment() {
    return 128;
}
}  // extern "C"

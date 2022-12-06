#include "runtime_internal.h"

extern "C" WEAK int halide_malloc_alignment() {
    return 128;
}

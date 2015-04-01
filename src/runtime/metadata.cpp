#include "HalideRuntime.h"

// This is a trick used to ensure that halide_filter_metadata_t (and related types)
// are included in the runtime bitcode; since no (other) runtime function references them
// they are ordinarily stripped, making Codegen_LLVM fail when we attempt to access
// the types.

#define INLINE inline __attribute__((weak)) __attribute__((used)) __attribute__((always_inline)) __attribute__((nothrow)) __attribute__((pure))

INLINE void __force_include_halide_filter_metadata_t_types(halide_filter_metadata_t*) {}

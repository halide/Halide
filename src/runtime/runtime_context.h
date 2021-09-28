#ifndef HALIDE_RUNTIME_CONTEXT_H
#define HALIDE_RUNTIME_CONTEXT_H

#include "HalideRuntime.h"

HALIDE_ATTRIBUTE_ALIGN(128)
typedef struct halide_runtime_globals_t {
    HALIDE_ATTRIBUTE_ALIGN(128)
    int rt_start_unused;
} halide_runtime_globals_t;

static_assert(sizeof(halide_context_t::reserved) >= sizeof(halide_runtime_globals_t));

#endif  // HALIDE_RUNTIME_CONTEXT_H

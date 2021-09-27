#ifndef HALIDE_RUNTIME_CONTEXT_H
#define HALIDE_RUNTIME_CONTEXT_H

#include "HalideRuntime.h"

typedef struct halide_context_internal_t {
    void *user_context;
    halide_runtime_function_table_t fns;

    HALIDE_ATTRIBUTE_ALIGN(128)
    int rt_start_unused;

} halide_context_internal_t;

static_assert(sizeof(halide_context_t) >= sizeof(halide_context_internal_t));
static_assert(__builtin_offsetof(halide_context_t, reserved) == __builtin_offsetof(halide_context_internal_t, rt_start_unused));


#endif  // HALIDE_RUNTIME_CONTEXT_H


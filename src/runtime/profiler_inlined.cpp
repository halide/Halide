#include "HalideRuntime.h"
#include "runtime_atomics.h"

extern "C" {

WEAK_INLINE int halide_profiler_set_current_func(halide_profiler_state *state, int pipeline, int func, int *sampling_token) {
    if (sampling_token == nullptr || *sampling_token == 0) {

        // Use empty volatile asm blocks to prevent code motion. Otherwise
        // llvm reorders or elides the stores.
        volatile int *ptr = &(state->current_func);
        // clang-format off
        asm volatile ("":::);
        *ptr = pipeline + func;
        asm volatile ("":::);
        // clang-format on
    }
    return 0;
}

// Invariant: shared xor local, and both are either 0 or 1. 0 means acquired.
WEAK_INLINE int halide_profiler_acquire_sampling_token(int32_t *shared, int32_t *local) {
    using namespace Halide::Runtime::Internal::Synchronization;

    *local = atomic_exchange_acquire(shared, 1);
    return 0;
}

WEAK_INLINE int halide_profiler_release_sampling_token(int32_t *shared, int32_t *local) {
    using namespace Halide::Runtime::Internal::Synchronization;

    if (*local == 0) {
        int32_t value = 0;
        atomic_store_release(shared, &value);
        *local = 1;
    }
    return 0;
}

WEAK_INLINE int halide_profiler_init_sampling_token(int32_t *sampling_token, int val) {
    *sampling_token = val;
    return 0;
}

WEAK_INLINE int halide_profiler_incr_active_threads(halide_profiler_state *state) {
    using namespace Halide::Runtime::Internal::Synchronization;

    return atomic_fetch_add_sequentially_consistent(&(state->active_threads), 1);
}

WEAK_INLINE int halide_profiler_decr_active_threads(halide_profiler_state *state) {
    using namespace Halide::Runtime::Internal::Synchronization;

    return atomic_fetch_sub_sequentially_consistent(&(state->active_threads), 1);
}
}

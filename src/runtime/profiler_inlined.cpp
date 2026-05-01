#include "HalideRuntime.h"
#include "runtime_atomics.h"

#include "printer.h"

extern "C" {

WEAK_INLINE int halide_profiler_set_current_func(halide_profiler_instance_state *instance, int func, int *sampling_token) {
    if (sampling_token == nullptr || *sampling_token == 0) {

        // Use empty volatile asm blocks to prevent code motion. Otherwise
        // llvm reorders or elides the stores.
        volatile int *ptr = &(instance->current_func);

        asm volatile("" :::);
        *ptr = func;
        asm volatile("" :::);
    }
    return 0;
}

// Called once we're sure we're not in bounds query code
WEAK_INLINE int halide_profiler_enable_instance(halide_profiler_instance_state *instance) {
    instance->should_collect_statistics = 1;
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

WEAK_INLINE int halide_profiler_incr_active_threads(halide_profiler_instance_state *instance) {
    using namespace Halide::Runtime::Internal::Synchronization;

    return atomic_fetch_add_sequentially_consistent(&(instance->active_threads), 1);
}

WEAK_INLINE int halide_profiler_decr_active_threads(halide_profiler_instance_state *instance) {
    using namespace Halide::Runtime::Internal::Synchronization;

    return atomic_fetch_sub_sequentially_consistent(&(instance->active_threads), 1);
}

WEAK_INLINE int halide_profiler_update_counters(struct halide_profiler_instance_state *instance,
                                                int id,
                                                uint64_t realizations,
                                                uint64_t productions,
                                                uint64_t parallel_loops,
                                                uint64_t parallel_tasks,
                                                uint64_t points_required_at_realization,
                                                uint64_t points_required_at_root,
                                                uint64_t scalar_loads,
                                                uint64_t vector_loads,
                                                uint64_t gathers,
                                                uint64_t bytes_loaded,
                                                uint64_t scalar_stores,
                                                uint64_t vector_stores,
                                                uint64_t scatters,
                                                uint64_t bytes_stored) {
    using namespace Halide::Runtime::Internal::Synchronization;

    halide_profiler_func_stats &stats = instance->funcs[id];

    // This gets inlined. If this is in an inner loop, most of the args will be
    // the constant zero. We therefore test for zero before adding to every
    // counter so that unused counters compile to no code.
#define UPDATE_COUNTER(X)                                        \
    if (X) {                                                     \
        atomic_fetch_add_sequentially_consistent(&(stats.X), X); \
    }

    UPDATE_COUNTER(realizations);
    UPDATE_COUNTER(productions);
    UPDATE_COUNTER(parallel_loops);
    UPDATE_COUNTER(parallel_tasks);
    UPDATE_COUNTER(points_required_at_realization);
    UPDATE_COUNTER(points_required_at_root);
    UPDATE_COUNTER(scalar_loads);
    UPDATE_COUNTER(vector_loads);
    UPDATE_COUNTER(gathers);
    UPDATE_COUNTER(bytes_loaded);
    UPDATE_COUNTER(scalar_stores);
    UPDATE_COUNTER(vector_stores);
    UPDATE_COUNTER(scatters);
    UPDATE_COUNTER(bytes_stored);

#undef UPDATE_COUNTER
    return 0;
}
}

// Glue that builds the vendored copy of Halide's default thread pool
// (thread_pool_common.h, see README.md) into the app as ordinary C++.
//
// The Halide runtime defines every thread-pool entry point (halide_do_par_for,
// halide_do_parallel_tasks, halide_semaphore_*, halide_set_num_threads,
// halide_shutdown_thread_pool, ...) as a weak symbol. This file defines them
// all strongly, so the linker binds the kernels' calls, and the runtime's own
// internal calls, to this copy. It must be linked as an object file, not from
// an archive: an archive member is only loaded for undefined symbols, and the
// runtime's weak definitions would already satisfy them.
//
// The pool's C++ internals (work_queue, worker_thread, ...) are wrapped in a
// namespace of their own, so they can't collide with the runtime's weak copies
// of the same names, which have their own layout.
#include "thread_pool.h"
#include "HalideRuntime.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
// Exported by the Halide runtime, but not declared in HalideRuntime.h.
int halide_host_cpu_count();
void halide_thread_yield();
}

// What runtime_internal.h and runtime_atomics.h provide to runtime code.
#define WEAK
#define ALWAYS_INLINE inline __attribute__((always_inline))
#define halide_abort_if_false(user_context, cond)                                      \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            halide_print(user_context, "halide_abort_if_false() failed: " #cond "\n"); \
            abort();                                                                   \
        }                                                                              \
    } while (0)

// The copy's additions to the runtime API get app names, so that they can't
// collide with the runtime's if they are ever added upstream.
#define halide_thread_pool_keep_awake ggml_halide_thread_pool_keep_awake

namespace ggml_halide_pool {

constexpr int MAX_THREADS = 256;

namespace Halide::Runtime::Internal::Synchronization {

template<typename T>
ALWAYS_INLINE T atomic_fetch_add_acquire_release(T *addr, T val) {
    return __atomic_fetch_add(addr, val, __ATOMIC_ACQ_REL);
}

template<typename T, typename TV>
ALWAYS_INLINE TV atomic_add_fetch_sequentially_consistent(T *addr, TV val) {
    return __atomic_add_fetch(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T>
ALWAYS_INLINE bool atomic_cas_weak_relacq_relaxed(T *addr, T *expected, T *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_load_relaxed(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_load_acquire(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_ACQUIRE);
}

template<typename T>
ALWAYS_INLINE void atomic_store_release(T *addr, T *val) {
    __atomic_store(addr, val, __ATOMIC_RELEASE);
}

}  // namespace Halide::Runtime::Internal::Synchronization

#include "thread_pool_common.h"

}  // namespace ggml_halide_pool

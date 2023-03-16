#ifndef HALIDE_RUNTIME_RUNTIME_ATOMICS_H
#define HALIDE_RUNTIME_RUNTIME_ATOMICS_H

// This file provides an abstraction layer over the __sync/__atomic builtins
// in Clang; for various reasons, we use __sync for 32-bit targets, and
// __atomic for 64-bit. At some point it may be desirable/necessary to
// migrate 32-bit to __atomic as well, at which time this file can
// likely go away. See https://github.com/halide/Halide/issues/7431 for
// a discussion of the history and issues as to why we work this way.

#include "HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Synchronization {

namespace {

// TODO: most of these wrappers should do the remove_volatile for secondary arguments;
// I've only put it in place for the locations necessary at this time.
template<class T>
struct remove_volatile { typedef T type; };
template<class T>
struct remove_volatile<volatile T> { typedef T type; };

#ifdef BITS_32
ALWAYS_INLINE uintptr_t atomic_and_fetch_release(uintptr_t *addr, uintptr_t val) {
    return __sync_and_and_fetch(addr, val);
}

template<typename T>
ALWAYS_INLINE T atomic_fetch_add_acquire_release(T *addr, T val) {
    return __sync_fetch_and_add(addr, val);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_add_sequentially_consistent(T *addr, TV val) {
    return __sync_fetch_and_add(addr, val);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_sub_sequentially_consistent(T *addr, TV val) {
    return __sync_fetch_and_sub(addr, val);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_or_sequentially_consistent(T *addr, TV val) {
    return __sync_fetch_and_or(addr, val);
}

template<typename T>
ALWAYS_INLINE T atomic_add_fetch_sequentially_consistent(T *addr, T val) {
    return __sync_add_and_fetch(addr, val);
}

template<typename T>
ALWAYS_INLINE T atomic_sub_fetch_sequentially_consistent(T *addr, T val) {
    return __sync_sub_and_fetch(addr, val);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE bool cas_strong_sequentially_consistent_helper(T *addr, TV *expected, TV *desired) {
    TV oldval = *expected;
    TV gotval = __sync_val_compare_and_swap(addr, oldval, *desired);
    *expected = gotval;
    return oldval == gotval;
}

ALWAYS_INLINE bool atomic_cas_strong_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE bool atomic_cas_strong_sequentially_consistent(T *addr, TV *expected, TV *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

template<typename T>
ALWAYS_INLINE bool atomic_cas_weak_relacq_relaxed(T *addr, T *expected, T *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_relaxed_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

ALWAYS_INLINE bool atomic_cas_weak_acquire_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return cas_strong_sequentially_consistent_helper(addr, expected, desired);
}

template<typename T>
ALWAYS_INLINE T atomic_fetch_and_release(T *addr, T val) {
    return __sync_fetch_and_and(addr, val);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_and_sequentially_consistent(T *addr, TV val) {
    return __sync_fetch_and_and(addr, val);
}

template<typename T>
ALWAYS_INLINE void atomic_load_relaxed(T *addr, T *val) {
    *val = *addr;
}

template<typename T>
ALWAYS_INLINE void atomic_load_acquire(T *addr, T *val) {
    __sync_synchronize();
    *val = *addr;
}

template<typename T>
ALWAYS_INLINE T atomic_exchange_acquire(T *addr, T val) {
    // Despite the name, this is really just an exchange operation with acquire ordering.
    return __sync_lock_test_and_set(addr, val);
}

ALWAYS_INLINE uintptr_t atomic_or_fetch_relaxed(uintptr_t *addr, uintptr_t val) {
    return __sync_or_and_fetch(addr, val);
}

ALWAYS_INLINE void atomic_store_relaxed(uintptr_t *addr, uintptr_t *val) {
    *addr = *val;
}

template<typename T>
ALWAYS_INLINE void atomic_store_release(T *addr, T *val) {
    *addr = *val;
    __sync_synchronize();
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE void atomic_store_sequentially_consistent(T *addr, TV *val) {
    *addr = *val;
    __sync_synchronize();
}

ALWAYS_INLINE void atomic_thread_fence_acquire() {
    __sync_synchronize();
}

ALWAYS_INLINE void atomic_thread_fence_sequentially_consistent() {
    __sync_synchronize();
}

#else

ALWAYS_INLINE uintptr_t atomic_and_fetch_release(uintptr_t *addr, uintptr_t val) {
    return __atomic_and_fetch(addr, val, __ATOMIC_RELEASE);
}

template<typename T>
ALWAYS_INLINE T atomic_fetch_add_acquire_release(T *addr, T val) {
    return __atomic_fetch_add(addr, val, __ATOMIC_ACQ_REL);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_add_sequentially_consistent(T *addr, TV val) {
    return __atomic_fetch_add(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_sub_sequentially_consistent(T *addr, TV val) {
    return __atomic_fetch_sub(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE T atomic_fetch_or_sequentially_consistent(T *addr, TV val) {
    return __atomic_fetch_or(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T>
ALWAYS_INLINE T atomic_add_fetch_sequentially_consistent(T *addr, T val) {
    return __atomic_add_fetch(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T>
ALWAYS_INLINE T atomic_sub_fetch_sequentially_consistent(T *addr, T val) {
    return __atomic_sub_fetch(addr, val, __ATOMIC_SEQ_CST);
}

ALWAYS_INLINE bool atomic_cas_strong_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE bool atomic_cas_strong_sequentially_consistent(T *addr, TV *expected, TV *desired) {
    return __atomic_compare_exchange(addr, expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

template<typename T>
ALWAYS_INLINE bool atomic_cas_weak_relacq_relaxed(T *addr, T *expected, T *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_release_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_relaxed_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}

ALWAYS_INLINE bool atomic_cas_weak_acquire_relaxed(uintptr_t *addr, uintptr_t *expected, uintptr_t *desired) {
    return __atomic_compare_exchange(addr, expected, desired, true, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE uintptr_t atomic_fetch_and_release(T *addr, T val) {
    return __atomic_fetch_and(addr, val, __ATOMIC_RELEASE);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE uintptr_t atomic_fetch_and_sequentially_consistent(T *addr, TV val) {
    return __atomic_fetch_and(addr, val, __ATOMIC_SEQ_CST);
}

template<typename T>
ALWAYS_INLINE void atomic_load_relaxed(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_load_acquire(T *addr, T *val) {
    __atomic_load(addr, val, __ATOMIC_ACQUIRE);
    __sync_synchronize();
    *val = *addr;
}

template<typename T>
ALWAYS_INLINE T atomic_exchange_acquire(T *addr, T val) {
    T result;
    __atomic_exchange(addr, &val, &result, __ATOMIC_ACQUIRE);
    return result;
}

ALWAYS_INLINE uintptr_t atomic_or_fetch_relaxed(uintptr_t *addr, uintptr_t val) {
    return __atomic_or_fetch(addr, val, __ATOMIC_RELAXED);
}

ALWAYS_INLINE void atomic_store_relaxed(uintptr_t *addr, uintptr_t *val) {
    __atomic_store(addr, val, __ATOMIC_RELAXED);
}

template<typename T>
ALWAYS_INLINE void atomic_store_release(T *addr, T *val) {
    __atomic_store(addr, val, __ATOMIC_RELEASE);
}

template<typename T, typename TV = typename remove_volatile<T>::type>
ALWAYS_INLINE void atomic_store_sequentially_consistent(T *addr, TV *val) {
    __atomic_store(addr, val, __ATOMIC_SEQ_CST);
}

ALWAYS_INLINE void atomic_thread_fence_acquire() {
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

ALWAYS_INLINE void atomic_thread_fence_sequentially_consistent() {
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

#endif

}  // namespace

}  // namespace Synchronization
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_RUNTIME_RUNTIME_ATOMICS_H

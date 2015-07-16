#ifndef HALIDE_SCOPED_SPIN_LOCK_H
#define HALIDE_SCOPED_SPIN_LOCK_H

namespace Halide { namespace Runtime { namespace Internal {

// An RAII spin lock.
struct ScopedSpinLock {
    volatile int *lock;

    ScopedSpinLock(volatile int *l) __attribute__((always_inline)) : lock(l) {
        while (__sync_lock_test_and_set(lock, 1)) { }
    }

    ~ScopedSpinLock() __attribute__((always_inline)) {
        __sync_lock_release(lock);
    }
};

}}} // namespace Halide::Runtime::Internal

#endif

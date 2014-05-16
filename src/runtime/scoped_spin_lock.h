#ifndef HALIDE_SCOPED_SPIN_LOCK_H
#define HALIDE_SCOPED_SPIN_LOCK_H

// An RAII spin lock.
struct ScopedSpinLock {
    volatile int *lock;

    ScopedSpinLock(volatile int *l) : lock(l) {
        while (__sync_lock_test_and_set(lock, 1)) { }
    }

    ~ScopedSpinLock() {
        __sync_lock_release(lock);
    }
};

#endif

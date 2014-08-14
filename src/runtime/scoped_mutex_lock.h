#ifndef HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H
#define HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H

#include "HalideRuntime.h"

// Avoid ODR violations
namespace {

// An RAII mutex locking operation
struct ScopedMutexLock {
    halide_mutex *mutex;

    ScopedMutexLock(halide_mutex *mutex) : mutex(mutex) {
        halide_mutex_lock(mutex);
    }

    ~ScopedMutexLock() {
        halide_mutex_unlock(mutex);
    }
};

}

#endif

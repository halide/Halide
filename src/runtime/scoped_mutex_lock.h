#ifndef HALIDE_RUNTIME_MUTEX_H
#define HALIDE_RUNTIME_MUTEX_H

#include "HalideRuntime.h"

// Avoid ODR violations
namespace {

// An RAII mutex
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

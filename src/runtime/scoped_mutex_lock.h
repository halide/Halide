#ifndef HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H
#define HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

// An RAII mutex locking operation
struct ScopedMutexLock {
    halide_mutex *mutex;

    ScopedMutexLock(halide_mutex *mutex) __attribute__((always_inline)) : mutex(mutex) {
        halide_mutex_init(mutex);
        halide_mutex_lock(mutex);
    }

    ~ScopedMutexLock() __attribute__((always_inline)) {
        halide_mutex_unlock(mutex);
    }
};

}}} // namespace Halide::Runtime::Internal

#endif

#ifndef HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H
#define HALIDE_RUNTIME_SCOPED_MUTEX_LOCK_H

#include "HalideRuntime.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// An RAII mutex locking operation
struct ScopedMutexLock {
    halide_mutex *mutex;

    ALWAYS_INLINE explicit ScopedMutexLock(halide_mutex *mutex)
        : mutex(mutex) {
        halide_mutex_lock(mutex);
    }

    ALWAYS_INLINE ~ScopedMutexLock() {
        halide_mutex_unlock(mutex);
    }
};

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif

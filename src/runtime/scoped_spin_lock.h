#ifndef HALIDE_SCOPED_SPIN_LOCK_H
#define HALIDE_SCOPED_SPIN_LOCK_H

namespace Halide {
namespace Runtime {
namespace Internal {

// An RAII spin lock.
struct ScopedSpinLock {
    // Note that __atomic_test_and_set() requires use of a char (or bool)
    typedef char AtomicFlag;

    volatile AtomicFlag *const flag;

    ALWAYS_INLINE explicit ScopedSpinLock(volatile AtomicFlag *flag)
        : flag(flag) {
        while (__atomic_test_and_set(flag, __ATOMIC_ACQUIRE)) {
            // nothing
        }
    }

    ALWAYS_INLINE ~ScopedSpinLock() {
        __atomic_clear(flag, __ATOMIC_RELEASE);
    }
};

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif

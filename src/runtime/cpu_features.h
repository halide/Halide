#ifndef HALIDE_CPU_FEATURES_H
#define HALIDE_CPU_FEATURES_H

#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Return two masks:
// One with all the CPU-specific features that might possible be available on this architecture ('known'),
// and one with the subset that are actually present ('available').
struct CpuFeatures {
    static const int kWordCount = (halide_target_feature_end + 63) / (sizeof(uint64_t) * 8);

    ALWAYS_INLINE void set_known(int i) {
        known[i >> 6] |= ((uint64_t)1) << (i & 63);
    }

    ALWAYS_INLINE void set_available(int i) {
        available[i >> 6] |= ((uint64_t)1) << (i & 63);
    }

    ALWAYS_INLINE bool test_known(int i) const {
        return (known[i >> 6] & ((uint64_t)1) << (i & 63)) != 0;
    }

    ALWAYS_INLINE bool test_available(int i) const {
        return (available[i >> 6] & ((uint64_t)1) << (i & 63)) != 0;
    }

    ALWAYS_INLINE
    CpuFeatures() {
        // Can't use in-class initing of these without C++11 enabled,
        // which isn't the case for all runtime builds
        for (int i = 0; i < kWordCount; ++i) {
            known[i] = 0;
            available[i] = 0;
        }
    }

    uint64_t known[kWordCount];      // mask of the CPU features we know how to detect
    uint64_t available[kWordCount];  // mask of the CPU features that are available
                                     // (always a subset of 'known')
};

extern WEAK CpuFeatures halide_get_cpu_features();

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#endif  // HALIDE_CPU_FEATURES_H

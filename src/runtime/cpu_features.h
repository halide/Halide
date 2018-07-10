#ifndef HALIDE_CPU_FEATURES_H
#define HALIDE_CPU_FEATURES_H

#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide { namespace Runtime { namespace Internal {

// Return two masks:
// One with all the CPU-specific features that might possible be available on this architecture ('known'),
// and one with the subset that are actually present ('available').
struct CpuFeatures {
    static const int kWordCount = (halide_target_feature_end + 63) / (sizeof(uint64_t) * 8);

    __attribute__((always_inline))
    void set_known(int i) {
        known[i / 64] |= ((uint64_t) 1) << (i % 64);
    }

    __attribute__((always_inline))
    void set_available(int i) {
        available[i / 64] |= ((uint64_t) 1) << (i % 64);
    }

    __attribute__((always_inline))
    bool test_known(int i) const {
        return (known[i / 64] & ((uint64_t) 1) << (i % 64)) != 0;
    }

    __attribute__((always_inline))
    bool test_available(int i) const {
        return (available[i / 64] & ((uint64_t) 1) << (i % 64)) != 0;
    }

    uint64_t known[kWordCount] = {0};     // mask of the CPU features we know how to detect
    uint64_t available[kWordCount] = {0}; // mask of the CPU features that are available
                                          // (always a subset of 'known')
};

extern WEAK CpuFeatures halide_get_cpu_features();

}}} // namespace Halide::Runtime::Internal

#endif  // HALIDE_CPU_FEATURES_H

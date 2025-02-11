#ifndef HALIDE_CPU_FEATURES_H
#define HALIDE_CPU_FEATURES_H

#include "HalideRuntime.h"
#include "runtime_internal.h"

namespace Halide {
namespace Runtime {
namespace Internal {

// Size of CPU feature mask large enough to cover all Halide target features
static constexpr int cpu_feature_mask_size = (halide_target_feature_end + 63) / (sizeof(uint64_t) * 8);

// Contains two masks:
// One with all the CPU-specific features that might possible be available on this architecture ('known'),
// and one with the subset that are actually present ('available').
struct CpuFeatures {
    uint64_t known[cpu_feature_mask_size] = {0};      // mask of the CPU features we know how to detect
    uint64_t available[cpu_feature_mask_size] = {0};  // mask of the CPU features that are available
                                                      // (always a subset of 'known')
};

ALWAYS_INLINE void halide_set_known_cpu_feature(CpuFeatures *features, int i) {
    features->known[i >> 6] |= ((uint64_t)1) << (i & 63);
}

ALWAYS_INLINE void halide_set_available_cpu_feature(CpuFeatures *features, int i) {
    features->available[i >> 6] |= ((uint64_t)1) << (i & 63);
}

ALWAYS_INLINE bool halide_test_known_cpu_feature(CpuFeatures *features, int i) {
    return (features->known[i >> 6] & ((uint64_t)1) << (i & 63)) != 0;
}

ALWAYS_INLINE bool halide_test_available_cpu_feature(CpuFeatures *features, int i) {
    return (features->available[i >> 6] & ((uint64_t)1) << (i & 63)) != 0;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

// NOTE: This method is not part of the public API, but we push it into extern "C" to
//       avoid name mangling mismatches between platforms. See: https://github.com/halide/Halide/issues/8565
extern "C" WEAK int halide_get_cpu_features(Halide::Runtime::Internal::CpuFeatures *features);

#endif  // HALIDE_CPU_FEATURES_H

#include "HalideRuntime.h"

namespace Halide { namespace Runtime { namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    uint64_t known = 1ULL << halide_target_feature_no_neon;

    // All ARM architectures support "No Neon".
    uint64_t available = 1ULL << halide_target_feature_no_neon;

    // TODO: add runtime detection for ARMv7s. AFAICT Apple doesn't
    // provide an Officially Approved Way to detect this at runtime.
    // Could probably use some variant of sysctl() to detect, but would
    // need some experimentation and testing to get right.
    // known |= 1ULL << halide_target_feature_armv7s;
    // if () {
    //    available |= 1ULL << halide_target_feature_armv7s;
    // }

    CpuFeatures features = {known, available};
    return features;
}

}}} // namespace Halide::Runtime::Internal

#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;
    // All ARM architectures support "No Neon".
    features.set_known(halide_target_feature_no_neon);
    features.set_available(halide_target_feature_no_neon);

    // TODO: add runtime detection for ARMv7s. AFAICT Apple doesn't
    // provide an Officially Approved Way to detect this at runtime.
    // Could probably use some variant of sysctl() to detect, but would
    // need some experimentation and testing to get right.
    // features.set_known(halide_target_feature_armv7s);
    // if () {
    //    features.set_available(halide_target_feature_armv7s);
    // }

    return features;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

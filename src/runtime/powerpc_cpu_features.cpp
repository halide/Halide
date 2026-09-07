#include "HalideRuntime.h"
#include "auxv_ops.h"
#include "cpu_features.h"
#include "powerpc_cpu_detect.h"

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    using namespace Halide::Internal::CpuDetect;

    // The set of features we know how to detect must match what the shared
    // detection logic can actually report, so take both from the same list.
    for_each_detectable_powerpc_feature(
        [&](halide_target_feature_t f) { halide_set_known_cpu_feature(features, f); });

    GetAuxValOps<AvailableCpuFeatureSink> ops{{features}};
    (void)detect_powerpc_features(ops);

    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

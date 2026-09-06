#include "HalideRuntime.h"
#include "arm_cpu_detect.h"
#include "auxv_ops.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

using namespace Halide::Internal::CpuDetect;

#if LINUX
using PlatformOps = GetAuxValOps<AvailableCpuFeatureSink>;

#elif OSX
using PlatformOps = SysctlByNameOps<AvailableCpuFeatureSink>;

#elif WINDOWS
using PlatformOps = WindowsProcessorFeatureOps<AvailableCpuFeatureSink>;

#else
struct PlatformOps : AvailableCpuFeatureSink {
    using ArmDetectionSource = NullSource;
};

#endif

#if BITS_64
constexpr ArmArch arm_arch = ArmArch::Arm64;
#else
constexpr ArmArch arm_arch = ArmArch::Arm32;
#endif

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    // The set of features we know about must match the shared detection logic,
    // so take both from the same list.
    for_each_detectable_arm_feature(
        [&](halide_target_feature_t f) { halide_set_known_cpu_feature(features, f); });

    // Every ARM host can run code targeted with "No Neon". This isn't detected,
    // so it isn't part of the shared list.
    halide_set_known_cpu_feature(features, halide_target_feature_no_neon);
    halide_set_available_cpu_feature(features, halide_target_feature_no_neon);

    PlatformOps ops{{features}};
    (void)detect_arm_features(ops, arm_arch);

    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

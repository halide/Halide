#include "HalideRuntime.h"
#include "cpu_features.h"

#define AT_HWCAP 16
#define AT_HWCAP2 26

#define PPC_FEATURE_HAS_VSX 0x00000080

#define PPC_FEATURE2_ARCH_2_07 0x80000000

extern "C" unsigned long int getauxval(unsigned long int);

namespace Halide {
namespace Runtime {
namespace Internal {

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;
    features.set_known(halide_target_feature_vsx);
    features.set_known(halide_target_feature_power_arch_2_07);

    const unsigned long hwcap = getauxval(AT_HWCAP);
    const unsigned long hwcap2 = getauxval(AT_HWCAP2);

    if (hwcap & PPC_FEATURE_HAS_VSX) {
        features.set_available(halide_target_feature_vsx);
    }
    if (hwcap2 & PPC_FEATURE2_ARCH_2_07) {
        features.set_available(halide_target_feature_power_arch_2_07);
    }
    return features;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

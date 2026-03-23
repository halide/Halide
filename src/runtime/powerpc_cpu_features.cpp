#include "HalideRuntime.h"
#include "cpu_features.h"

#define AT_HWCAP 16
#define AT_HWCAP2 26

#define PPC_FEATURE_HAS_VSX 0x00000080

#define PPC_FEATURE2_ARCH_2_07 0x80000000

extern "C" {

unsigned long int getauxval(unsigned long int);
}

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    halide_set_known_cpu_feature(features, halide_target_feature_vsx);
    halide_set_known_cpu_feature(features, halide_target_feature_power_arch_2_07);

    const unsigned long hwcap = getauxval(AT_HWCAP);
    const unsigned long hwcap2 = getauxval(AT_HWCAP2);

    if (hwcap & PPC_FEATURE_HAS_VSX) {
        halide_set_available_cpu_feature(features, halide_target_feature_vsx);
    }
    if (hwcap2 & PPC_FEATURE2_ARCH_2_07) {
        halide_set_available_cpu_feature(features, halide_target_feature_power_arch_2_07);
    }
    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

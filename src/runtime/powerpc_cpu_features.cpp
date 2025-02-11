#include "HalideRuntime.h"
#include "cpu_features.h"

#define AT_HWCAP 16
#define AT_HWCAP2 26

#define PPC_FEATURE_HAS_VSX 0x00000080

#define PPC_FEATURE2_ARCH_2_07 0x80000000

extern "C" {

unsigned long int getauxval(unsigned long int);

WEAK int halide_get_cpu_features(Halide::Runtime::Internal::CpuFeatures *features) {
    Halide::Runtime::Internal::halide_set_known_cpu_feature(features, halide_target_feature_vsx);
    Halide::Runtime::Internal::halide_set_known_cpu_feature(features, halide_target_feature_power_arch_2_07);

    const unsigned long hwcap = getauxval(AT_HWCAP);
    const unsigned long hwcap2 = getauxval(AT_HWCAP2);

    if (hwcap & PPC_FEATURE_HAS_VSX) {
        Halide::Runtime::Internal::halide_set_available_cpu_feature(features, halide_target_feature_vsx);
    }
    if (hwcap2 & PPC_FEATURE2_ARCH_2_07) {
        Halide::Runtime::Internal::halide_set_available_cpu_feature(features, halide_target_feature_power_arch_2_07);
    }
    return halide_error_code_success;
}

}  // extern "C" linkage

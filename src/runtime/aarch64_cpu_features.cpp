#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#if LINUX

extern "C" unsigned long getauxval(unsigned long type);

#define AT_HWCAP 16
#define AT_HWCAP2 26

// https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/kernel/uapi/asm-arm64/asm/hwcap.h
// https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/hwcap.h
#define HWCAP_ASIMDHP (1 << 10)
#define HWCAP_ASIMDDP (1 << 20)
#define HWCAP_SVE (1 << 22)
#define HWCAP2_SVE2 (1 << 1)

namespace {

void set_platform_features(CpuFeatures &features) {
    unsigned long hwcaps = getauxval(AT_HWCAP);
    unsigned long hwcaps2 = getauxval(AT_HWCAP2);

    if (hwcaps & HWCAP_ASIMDDP) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    if (hwcaps & HWCAP_ASIMDHP) {
        features.set_available(halide_target_feature_arm_fp16);
    }

    if (hwcaps & HWCAP_SVE) {
        features.set_available(halide_target_feature_sve);
    }

    if (hwcaps2 & HWCAP2_SVE2) {
        features.set_available(halide_target_feature_sve2);
    }
}

}  // namespace

#elif OSX

extern "C" int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

namespace {

bool sysctl_is_set(const char *name) {
    int enabled = 0;
    size_t enabled_len = sizeof(enabled);
    return sysctlbyname(name, &enabled, &enabled_len, nullptr, 0) == 0 && enabled;
}

void set_platform_features(CpuFeatures &features) {
    if (sysctl_is_set("hw.optional.arm.FEAT_DotProd")) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    if (sysctl_is_set("hw.optional.arm.FEAT_FP16")) {
        features.set_available(halide_target_feature_arm_fp16);
    }
}

}  // namespace

#elif WINDOWS

typedef int BOOL;
typedef unsigned long DWORD;

extern "C" BOOL IsProcessorFeaturePresent(DWORD feature);

#define PF_FLOATING_POINT_EMULATED (1)
#define PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE (27)
#define PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE (43)

// Magic value from: https://github.com/dotnet/runtime/blob/7e977dcbe5efaeec2c75ed0c3e200c85b2e55522/src/native/minipal/cpufeatures.c#L19
#define PF_ARM_SVE_INSTRUCTIONS_AVAILABLE (46)

namespace {

void set_platform_features(CpuFeatures &features) {
    // This is the strategy used by Google's cpuinfo library for
    // detecting fp16 arithmetic support on Windows.
    if (!IsProcessorFeaturePresent(PF_FLOATING_POINT_EMULATED) &&
        IsProcessorFeaturePresent(PF_ARM_FMAC_INSTRUCTIONS_AVAILABLE)) {
        features.set_available(halide_target_feature_arm_fp16);
    }

    if (IsProcessorFeaturePresent(PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE)) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    if (IsProcessorFeaturePresent(PF_ARM_SVE_INSTRUCTIONS_AVAILABLE)) {
        features.set_available(halide_target_feature_sve);
    }
}

}  // namespace

#else

namespace {

void set_platform_features(CpuFeatures &) {
}

}  // namespace

#endif

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;
    features.set_known(halide_target_feature_arm_dot_prod);
    features.set_known(halide_target_feature_arm_fp16);
    features.set_known(halide_target_feature_armv7s);
    features.set_known(halide_target_feature_no_neon);
    features.set_known(halide_target_feature_sve);
    features.set_known(halide_target_feature_sve2);

    // All ARM architectures support "No Neon".
    features.set_available(halide_target_feature_no_neon);

    set_platform_features(features);

    return features;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

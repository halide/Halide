#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#if ANDROID

extern unsigned long getauxval(unsigned long type);
#define AT_HWCAP 16
//#define AT_HWCAP2 26

#define HWCAP_VFPv4 (1 << 16)
#define HWCAP_ASIMDDP (1 << 20)

static void set_platform_features(CpuFeatures &features) {
    unsigned long hwcaps = getauxval(AT_HWCAP);

    // runtime detection for ARMDotProd extension
    if (hwcaps & HWCAP_ASIMDDP) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    // runtime detection for ARMFp16 extension
    if (hwcaps & HWCAP_VFPv4) {
        // VFPv4 is the only 32-bit arm-fp revision to support fp16
        features.set_available(halide_target_feature_arm_fp16);
    }
}

#elif OSX

extern int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

static bool sysctl_is_set(const char *name) {
    int enabled = 0;
    size_t enabled_len = sizeof(enabled);
    return sysctlbyname(name, &enabled, &enabled_len, nullptr, 0) == 0 && enabled;
}

static void set_platform_features(CpuFeatures &features) {
    // TODO: add runtime detection for ARMv7s. AFAICT Apple doesn't
    // provide an Officially Approved Way to detect this at runtime.
    // Could probably use some variant of sysctl() to detect, but would
    // need some experimentation and testing to get right.
    // features.set_known(halide_target_feature_armv7s);
    // if () {
    //    features.set_available(halide_target_feature_armv7s);
    // }

    // runtime detection for ARMDotProd extension
    if (sysctl_is_set("hw.optional.arm.FEAT_DotProd")) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    // runtime detection for ARMFp16 extension
    if (sysctl_is_set("hw.optional.arm.FEAT_FP16")) {
        features.set_available(halide_target_feature_arm_fp16);
    }
}

#else

static void set_platform_features(CpuFeatures &) {
    // TODO: add runtime detection for ARMDotProd extension
    // https://github.com/halide/Halide/issues/4727

    // TODO: add runtime detection for ARMFp16 extension
    // https://github.com/halide/Halide/issues/6106
}

#endif

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;
    // All ARM architectures support "No Neon".
    features.set_known(halide_target_feature_no_neon);
    features.set_available(halide_target_feature_no_neon);

    features.set_known(halide_target_feature_arm_dot_prod);
    features.set_known(halide_target_feature_arm_fp16);

    set_platform_features(features);

    return features;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#if LINUX

extern unsigned long getauxval(unsigned long type);

#define AT_HWCAP 16
#define AT_HWCAP2 26

// https://cs.android.com/android/platform/superproject/main/+/main:bionic/libc/kernel/uapi/asm-arm64/asm/hwcap.h
// https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/hwcap.h
#define HWCAP_FPHP (1 << 9)
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

    if (hwcaps & HWCAP_FPHP) {
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

extern int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

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

    // Apple M3 sets FEAT_SME to 0 and does not define FEAT_SVE
    // or FEAT_SVE2. As the M4 will be ARMv9.2-A, it will have
    // SVE, SVE2, and SME. We do not currently have an SME feature,
    // but we can at least be sure that FEAT_SME implies the two
    // SVE features.
    if (sysctl_is_set("hw.optional.arm.FEAT_SME")) {
        features.set_available(halide_target_feature_sve);
        features.set_available(halide_target_feature_sve2);
    }
}

}  // namespace

#else

namespace {

void set_platform_features(CpuFeatures &) {
    // TODO: Windows detection
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

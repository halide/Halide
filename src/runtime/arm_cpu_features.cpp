#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#if ANDROID

extern unsigned long getauxval(unsigned long type);
#define AT_HWCAP 16
// #define AT_HWCAP2 26

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

typedef int integer_t;

typedef integer_t cpu_type_t;
typedef integer_t cpu_subtype_t;

#define CPU_TYPE_ARM ((cpu_type_t)12)
#define CPU_SUBTYPE_ARM_V7S ((cpu_subtype_t)11) /* Swift */

extern int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

static bool sysctl_is_set(const char *name) {
    int enabled = 0;
    size_t enabled_len = sizeof(enabled);
    return sysctlbyname(name, &enabled, &enabled_len, nullptr, 0) == 0 && enabled;
}

static bool is_armv7s() {
    cpu_type_t type;
    size_t type_len = sizeof(type);
    if (sysctlbyname("hw.cputype", &type, &type_len, nullptr, 0)) {
        return false;
    }

    cpu_subtype_t subtype;
    size_t subtype_len = sizeof(subtype);
    if (sysctlbyname("hw.cpusubtype", &subtype, &subtype_len, nullptr, 0)) {
        return false;
    }

    return type == CPU_TYPE_ARM && subtype == CPU_SUBTYPE_ARM_V7S;
}

static void set_platform_features(CpuFeatures &features) {
    // runtime detection for ARMv7s
    features.set_known(halide_target_feature_armv7s);
    if (is_armv7s()) {
        features.set_available(halide_target_feature_armv7s);
    }

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

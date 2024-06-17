#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

#if LINUX

extern "C" unsigned long getauxval(unsigned long type);

#define AT_HWCAP 16

// https://cs.android.com/android/platform/superproject/+/master:bionic/libc/kernel/uapi/asm-arm/asm/hwcap.h
// https://github.com/torvalds/linux/blob/master/arch/arm/include/uapi/asm/hwcap.h
#define HWCAP_FPHP (1 << 22)
#define HWCAP_ASIMDDP (1 << 24)

namespace {

void set_platform_features(CpuFeatures &features) {
    unsigned long hwcaps = getauxval(AT_HWCAP);

    if (hwcaps & HWCAP_ASIMDDP) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    if (hwcaps & HWCAP_FPHP) {
        features.set_available(halide_target_feature_arm_fp16);
    }
}

}  // namespace

#elif OSX

typedef int integer_t;

typedef integer_t cpu_type_t;
typedef integer_t cpu_subtype_t;

#define CPU_TYPE_ARM ((cpu_type_t)12)
#define CPU_SUBTYPE_ARM_V7S ((cpu_subtype_t)11) /* Swift */

extern "C" int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

namespace {

bool sysctl_is_set(const char *name) {
    int enabled = 0;
    size_t enabled_len = sizeof(enabled);
    return sysctlbyname(name, &enabled, &enabled_len, nullptr, 0) == 0 && enabled;
}

bool is_armv7s() {
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

void set_platform_features(CpuFeatures &features) {
    if (is_armv7s()) {
        features.set_available(halide_target_feature_armv7s);
    }

    if (sysctl_is_set("hw.optional.arm.FEAT_DotProd")) {
        features.set_available(halide_target_feature_arm_dot_prod);
    }

    if (sysctl_is_set("hw.optional.arm.FEAT_FP16")) {
        features.set_available(halide_target_feature_arm_fp16);
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

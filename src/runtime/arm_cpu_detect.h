/** \file
 *
 * Shared ARM host CPU detection logic, used by both libHalide and the runtime.
 *
 * See x86_cpu_detect.h for the constraints this code compiles under and the
 * rationale for the "ops" pattern. In short: the runtime is freestanding, so
 * everything platform-specific is injected by the caller, and the logic here
 * is a pure function of what the platform reports.
 *
 * There are three ways to ask an ARM machine what it can do, and which one is
 * available depends on the OS rather than the CPU, so there is one entry point
 * per platform instead of one overall. All of them need a setter:
 *
 *     void set_feature(halide_target_feature_t f);
 *
 * Sysctl-based ops additionally need:
 *
 *     using ArmDetectionSource = SysctlSource;
 *     bool sysctl_get_int(const char *name, int *value);
 *
 * Windows-based ops additionally need:
 *
 *     using ArmDetectionSource = WindowsSource;
 *     bool processor_feature_present(int feature);
 *
 * Auxv-based ops additionally need:
 *
 *     using ArmDetectionSource = AuxvSource;
 *     uint64_t get_hwcap();
 *     uint64_t get_hwcap2();
 *
 * Whether a detected feature implies scalable vector registers is returned in
 * ArmDetection, so that a caller who cares can measure their width.
 */

#ifndef HALIDE_RUNTIME_ARM_CPU_DETECT_H
#define HALIDE_RUNTIME_ARM_CPU_DETECT_H

#include "HalideRuntime.h"

#if defined(__APPLE__) || (defined(COMPILING_HALIDE_RUNTIME) && OSX)
extern "C" int sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
#endif

#if defined(_WIN32) || (defined(COMPILING_HALIDE_RUNTIME) && WINDOWS)
extern "C" int __stdcall IsProcessorFeaturePresent(unsigned long feature);
#endif

namespace Halide {
namespace Internal {
namespace CpuDetect {

struct SysctlSource {
};

struct WindowsSource {
};

struct AuxvSource {
};

struct NullSource {
};

#if defined(__APPLE__) || (defined(COMPILING_HALIDE_RUNTIME) && OSX)
/** Add the macOS sysctl operation to a feature sink. */
template<typename Base>
struct SysctlByNameOps : Base {
    using ArmDetectionSource = SysctlSource;

    inline bool sysctl_get_int(const char *name, int *value) {
        size_t size = sizeof(*value);
        return sysctlbyname(name, value, &size, nullptr, 0) == 0;
    }
};
#endif

#if defined(_WIN32) || (defined(COMPILING_HALIDE_RUNTIME) && WINDOWS)
/** Add the Windows processor-feature operation to a feature sink. */
template<typename Base>
struct WindowsProcessorFeatureOps : Base {
    using ArmDetectionSource = WindowsSource;

    inline bool processor_feature_present(int feature) {
        return IsProcessorFeaturePresent((unsigned long)feature) != 0;
    }
};
#endif

/** ARM comes in two ABIs that report their features differently, and only the
 * 64-bit one has SVE at all. */
enum class ArmArch {
    Arm32,
    Arm64,
};

/** Facts about the host that a caller may want to act on, but that aren't
 * target features. */
struct ArmDetection {
    /** A reported feature uses scalable vector registers. libHalide measures
     * their width; the runtime doesn't need to do anything with this. */
    bool have_scalable_vector;
};

/** We know how to detect SVE (as opposed to SVE2) on every platform below, but
 * Halide's plain-SVE support isn't ready to be turned on by default yet, so the
 * detection is deliberately masked off. Flipping this to true re-enables it for
 * the compiler and the runtime at the same time.
 *
 * Note that SVE stays in the runtime's set of *known* features while this is
 * false.
 *
 * TODO: https://github.com/halide/Halide/issues/8872
 */
constexpr bool sve_detection_enabled = false;

/** Every ARM target feature the functions below make an availability decision
 * about. The runtime uses this to build its mask of "features we know about". */
template<typename Fn>
void for_each_detectable_arm_feature(Fn fn) {
    fn(halide_target_feature_arm_dot_prod);
    fn(halide_target_feature_arm_fp16);
    fn(halide_target_feature_armv7s);
    fn(halide_target_feature_sve);
    fn(halide_target_feature_sve2);
}

namespace detail {

// Linux hwcap bits. The 32- and 64-bit ABIs assign different bits to the same
// features, so these are per-arch.
// https://github.com/torvalds/linux/blob/master/arch/arm64/include/uapi/asm/hwcap.h
// https://github.com/torvalds/linux/blob/master/arch/arm/include/uapi/asm/hwcap.h
constexpr uint64_t hwcap_asimdhp(ArmArch arch) {
    return arch == ArmArch::Arm64 ? (1ull << 10) : (1ull << 23);
}

constexpr uint64_t hwcap_asimddp(ArmArch arch) {
    return arch == ArmArch::Arm64 ? (1ull << 20) : (1ull << 24);
}

// AArch64 only.
constexpr uint64_t hwcap_sve = 1ull << 22;
constexpr uint64_t hwcap2_sve2 = 1ull << 1;

// Feature codes for Windows' IsProcessorFeaturePresent. Spelled in lower case
// because windows.h defines the upper-case names as macros.
// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-isprocessorfeaturepresent
constexpr int pf_floating_point_emulated = 1;
constexpr int pf_arm_fmac_instructions_available = 27;
constexpr int pf_arm_v82_dp_instructions_available = 43;
constexpr int pf_arm_sve_instructions_available = 46;
constexpr int pf_arm_sve2_instructions_available = 47;

// sysctl names and values used on Apple platforms.
constexpr int cpu_type_arm = 12;
constexpr int cpu_subtype_arm_v7s = 11; /* Swift */

template<typename Ops>
bool sysctl_is_set(Ops &ops, const char *name) {
    int value = 0;
    return ops.sysctl_get_int(name, &value) && value != 0;
}

template<typename Ops>
bool is_armv7s(Ops &ops) {
    int type = 0, subtype = 0;
    return ops.sysctl_get_int("hw.cputype", &type) &&
           ops.sysctl_get_int("hw.cpusubtype", &subtype) &&
           type == cpu_type_arm &&
           subtype == cpu_subtype_arm_v7s;
}

// The single gate for the masked-off SVE detection described above. The test
// is passed as a callable so that disabling it costs nothing at runtime (some
// platforms answer this question with a system call) while the detection code
// itself stays present and compiled.
template<typename Ops, typename Predicate>
bool set_sve_if_enabled(Ops &ops, Predicate present) {
    if (sve_detection_enabled && present()) {
        ops.set_feature(halide_target_feature_sve);
        return true;
    }
    return false;
}

template<typename Ops>
bool set_sve2_if_present(Ops &ops, bool present) {
    if (present) {
        ops.set_feature(halide_target_feature_sve2);
        return true;
    }
    return false;
}

}  // namespace detail

/** Detect ARM features from the Linux/Android auxiliary vector. */
template<typename Ops>
ArmDetection detect_arm_features(Ops &ops, ArmArch arch, AuxvSource) {
    const uint64_t hwcap = ops.get_hwcap();
    const uint64_t hwcap2 = ops.get_hwcap2();
    if (hwcap & detail::hwcap_asimddp(arch)) {
        ops.set_feature(halide_target_feature_arm_dot_prod);
    }

    if (hwcap & detail::hwcap_asimdhp(arch)) {
        ops.set_feature(halide_target_feature_arm_fp16);
    }

    ArmDetection detection{false};
    if (arch == ArmArch::Arm64) {
        const bool have_sve =
            detail::set_sve_if_enabled(ops, [&] { return (hwcap & detail::hwcap_sve) != 0; });
        const bool have_sve2 =
            detail::set_sve2_if_present(ops, (hwcap2 & detail::hwcap2_sve2) != 0);
        detection.have_scalable_vector = have_sve || have_sve2;
    }
    return detection;
}

/** Detect ARM features via sysctl, on Apple platforms. Apple silicon does not
 * implement SVE, so there is nothing to mask off here. */
template<typename Ops>
ArmDetection detect_arm_features(Ops &ops, ArmArch arch, SysctlSource) {
    // Only 32-bit ARM can be a Swift core; on arm64 the cputype won't match, so
    // skip the two sysctls entirely.
    if (arch == ArmArch::Arm32 && detail::is_armv7s(ops)) {
        ops.set_feature(halide_target_feature_armv7s);
    }

    if (detail::sysctl_is_set(ops, "hw.optional.arm.FEAT_DotProd")) {
        ops.set_feature(halide_target_feature_arm_dot_prod);
    }

    if (detail::sysctl_is_set(ops, "hw.optional.arm.FEAT_FP16")) {
        ops.set_feature(halide_target_feature_arm_fp16);
    }

    return {false};
}

/** Detect ARM features via IsProcessorFeaturePresent, on Windows. */
template<typename Ops>
ArmDetection detect_arm_features(Ops &ops, ArmArch arch, WindowsSource) {
    // This is the strategy used by Google's cpuinfo library for detecting fp16
    // arithmetic support on Windows.
    if (!ops.processor_feature_present(detail::pf_floating_point_emulated) &&
        ops.processor_feature_present(detail::pf_arm_fmac_instructions_available)) {
        ops.set_feature(halide_target_feature_arm_fp16);
    }

    if (ops.processor_feature_present(detail::pf_arm_v82_dp_instructions_available)) {
        ops.set_feature(halide_target_feature_arm_dot_prod);
    }

    ArmDetection detection{false};
    if (arch == ArmArch::Arm64) {
        const bool have_sve = detail::set_sve_if_enabled(ops, [&]() {
            return ops.processor_feature_present(detail::pf_arm_sve_instructions_available);
        });
        const bool have_sve2 = detail::set_sve2_if_present(
            ops, ops.processor_feature_present(detail::pf_arm_sve2_instructions_available));
        detection.have_scalable_vector = have_sve || have_sve2;
    }
    return detection;
}

template<typename Ops>
ArmDetection detect_arm_features(Ops &, ArmArch, NullSource) {
    return {false};
}

/** Detect ARM features via the operation source selected by Ops. */
template<typename Ops>
ArmDetection detect_arm_features(Ops &ops, ArmArch arch) {
    return detect_arm_features(ops, arch, typename Ops::ArmDetectionSource{});
}

}  // namespace CpuDetect
}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_RUNTIME_ARM_CPU_DETECT_H

/** \file
 *
 * Shared PowerPC host CPU detection logic.
 *
 * See x86_cpu_detect.h for the constraints this code compiles under and the
 * rationale for the "ops" pattern. Both Linux and FreeBSD report these
 * capabilities through the ELF auxiliary vector with the same bit assignments.
 *
 * The ops object needs only:
 *
 *     void set_feature(halide_target_feature_t f);
 */

#ifndef HALIDE_RUNTIME_POWERPC_CPU_DETECT_H
#define HALIDE_RUNTIME_POWERPC_CPU_DETECT_H

#include "HalideRuntime.h"

namespace Halide {
namespace Internal {
namespace CpuDetect {

/** Every PowerPC target feature the function below knows how to detect. The
 * runtime uses this to build its mask of "features we know about", which must
 * stay in sync with what we actually set. */
template<typename Fn>
void for_each_detectable_powerpc_feature(Fn fn) {
    fn(halide_target_feature_vsx);
    fn(halide_target_feature_power_arch_2_07);
}

namespace detail {

/** Auxiliary vector capability bits, from
 * https://github.com/torvalds/linux/blob/master/arch/powerpc/include/uapi/asm/cputable.h
 * FreeBSD uses the same values (see sys/powerpc/include/cpu.h); Target.cpp
 * static_asserts that against the platform headers wherever they're available.
 */
/// @{
constexpr uint64_t ppc_feature_has_altivec = 0x10000000;
constexpr uint64_t ppc_feature_has_vsx = 0x00000080;
constexpr uint64_t ppc_feature2_arch_2_07 = 0x80000000;
/// @}

}  // namespace detail

/** Facts about the host that a caller may want to act on, but that aren't
 * target features. */
struct PowerPCDetection {
    /** Halide's PowerPC backend assumes AltiVec. Callers should report a useful
     * error if this is false; the runtime has no way to complain, so it ignores
     * it. */
    bool have_altivec;
};

/** Detect PowerPC features from the ELF auxiliary vector.  */
template<typename Ops>
PowerPCDetection detect_powerpc_features(Ops &ops) {
    uint64_t hwcap = ops.get_hwcap();
    if (hwcap & detail::ppc_feature_has_vsx) {
        ops.set_feature(halide_target_feature_vsx);
    }

    uint64_t hwcap2 = ops.get_hwcap2();
    if (hwcap2 & detail::ppc_feature2_arch_2_07) {
        ops.set_feature(halide_target_feature_power_arch_2_07);
    }

    return {(hwcap & detail::ppc_feature_has_altivec) != 0};
}

}  // namespace CpuDetect
}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_RUNTIME_POWERPC_CPU_DETECT_H

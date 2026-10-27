#ifndef HALIDE_RUNTIME_AUXV_OPS_H
#define HALIDE_RUNTIME_AUXV_OPS_H

namespace Halide {
namespace Internal {
namespace CpuDetect {

struct AuxvSource;

extern "C" unsigned long getauxval(unsigned long type);

namespace detail {

// These values are consistent across Linux and FreeBSD.
constexpr unsigned long at_hwcap = 16;
constexpr unsigned long at_hwcap2 = 26;

}  // namespace detail

template<typename Base>
struct GetAuxValOps : Base {
    using ArmDetectionSource = AuxvSource;

    uint64_t get_hwcap() {
        return getauxval(detail::at_hwcap);
    }
    uint64_t get_hwcap2() {
        return getauxval(detail::at_hwcap2);
    }
};

}  // namespace CpuDetect
}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_RUNTIME_AUXV_OPS_H

#include "HalideRuntime.h"
#include "cpu_features.h"
#include "x86_cpu_detect.h"

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" void x86_cpuid_halide(int32_t *);
extern "C" void x64_cpuid_halide(int32_t *);
extern "C" void xgetbv_halide(int32_t *);

namespace {

using Halide::Internal::CpuDetect::CpuidResult;

constexpr bool use_64_bits = (sizeof(size_t) == 8);

// The platform half of the shared detection logic in x86_cpu_detect.h. The
// runtime is compiled for a generic target triple, so cpuid and xgetbv are
// reached through the hand-written IR in x86.ll rather than emitted inline.
// Only the feature set matters here; other detected facts are returned to the
// caller, which the runtime can ignore.
struct RuntimeOps : AvailableCpuFeatureSink {
    [[nodiscard]] ALWAYS_INLINE CpuidResult cpuid(uint32_t fn_id, uint32_t extra) {
        int32_t info[4] = {(int32_t)fn_id, (int32_t)extra, 0, 0};
        if constexpr (use_64_bits) {
            x64_cpuid_halide(info);
        } else {
            x86_cpuid_halide(info);
        }
        return {(uint32_t)info[0], (uint32_t)info[1], (uint32_t)info[2], (uint32_t)info[3]};
    }

    // Returns low 32 bits of XCR specified by xcr_id.
    [[nodiscard]] ALWAYS_INLINE uint64_t xgetbv(uint32_t xcr_id) {
        int32_t xcr_info[2] = {(int32_t)xcr_id, 0};
        xgetbv_halide(xcr_info);
        return (uint64_t)(uint32_t)xcr_info[0];
    }

    ALWAYS_INLINE void request_amx_os_permission() {
        Halide::Internal::CpuDetect::detail::request_linux_amx_permission();
    }
};

}  // namespace

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    // The set of features we know how to detect must match what the shared
    // detection logic can actually report, so take both from the same list.
    Halide::Internal::CpuDetect::for_each_detectable_x86_feature(
        [&](halide_target_feature_t f) { halide_set_known_cpu_feature(features, f); });

    RuntimeOps ops{{features}};
    (void)Halide::Internal::CpuDetect::detect_x86_features(ops);

    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

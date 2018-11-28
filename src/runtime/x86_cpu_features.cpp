#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide { namespace Runtime { namespace Internal {

extern "C" void x86_cpuid_halide(int32_t *);

static inline void cpuid(int32_t fn_id, int32_t *info) {
    info[0] = fn_id;
    x86_cpuid_halide(info);
}

WEAK CpuFeatures halide_get_cpu_features() {
    CpuFeatures features;
    features.set_known(halide_target_feature_sse41);
    features.set_known(halide_target_feature_avx);
    features.set_known(halide_target_feature_f16c);
    features.set_known(halide_target_feature_fma);
    features.set_known(halide_target_feature_avx2);
    features.set_known(halide_target_feature_avx512);
    features.set_known(halide_target_feature_avx512_knl);
    features.set_known(halide_target_feature_avx512_skylake);
    features.set_known(halide_target_feature_avx512_cannonlake);

    int32_t info[4];
    cpuid(1, info);

    const bool have_sse41  = (info[2] & (1 << 19)) != 0;
    const bool have_avx    = (info[2] & (1 << 28)) != 0;
    const bool have_f16c   = (info[2] & (1 << 29)) != 0;
    const bool have_rdrand = (info[2] & (1 << 30)) != 0;
    const bool have_fma    = (info[2] & (1 << 12)) != 0;
    if (have_sse41) {
        features.set_available(halide_target_feature_sse41);
    }
    if (have_avx) {
        features.set_available(halide_target_feature_avx);
    }
    if (have_f16c) {
        features.set_available(halide_target_feature_f16c);
    }
    if (have_fma) {
        features.set_available(halide_target_feature_fma);
    }

    const bool use_64_bits = (sizeof(size_t) == 8);
    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        int info2[4];
        cpuid(7, info2);
        const uint32_t avx2 = 1U << 5;
        const uint32_t avx512f = 1U << 16;
        const uint32_t avx512dq = 1U << 17;
        const uint32_t avx512pf = 1U << 26;
        const uint32_t avx512er = 1U << 27;
        const uint32_t avx512cd = 1U << 28;
        const uint32_t avx512bw = 1U << 30;
        const uint32_t avx512vl = 1U << 31;
        const uint32_t avx512ifma = 1U << 21;
        const uint32_t avx512 = avx512f | avx512cd;
        const uint32_t avx512_knl = avx512 | avx512pf | avx512er;
        const uint32_t avx512_skylake = avx512 | avx512vl | avx512bw | avx512dq;
        const uint32_t avx512_cannonlake = avx512_skylake | avx512ifma; // Assume ifma => vbmi
        if ((info2[1] & avx2) == avx2) {
            features.set_available(halide_target_feature_avx2);
        }
        if ((info2[1] & avx512) == avx512) {
            features.set_available(halide_target_feature_avx512);
            if ((info2[1] & avx512_knl) == avx512_knl) {
                features.set_available(halide_target_feature_avx512_knl);
            }
            if ((info2[1] & avx512_skylake) == avx512_skylake) {
                features.set_available(halide_target_feature_avx512_skylake);
            }
            if ((info2[1] & avx512_cannonlake) == avx512_cannonlake) {
                features.set_available(halide_target_feature_avx512_cannonlake);
            }
        }
    }
    return features;
}

}}}  // namespace Halide::Runtime::Internal

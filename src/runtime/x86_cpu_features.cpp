#include "HalideRuntime.h"


namespace Halide { namespace Runtime { namespace Internal {

extern "C" void x86_cpuid_halide(int32_t *);

static inline void cpuid(int32_t fn_id, int32_t *info) {
    info[0] = fn_id;
    x86_cpuid_halide(info);
}

WEAK CpuFeatures halide_get_cpu_features() {
    const uint64_t known = (1ULL << halide_target_feature_sse41) |
                           (1ULL << halide_target_feature_avx) |
                           (1ULL << halide_target_feature_f16c) |
                           (1ULL << halide_target_feature_fma) |
                           (1ULL << halide_target_feature_avx2);

    uint64_t available = 0;

    int32_t info[4];
    cpuid(1, info);

    const bool have_sse41 = (info[2] & (1 << 19)) != 0;
    const bool have_avx = (info[2] & (1 << 28)) != 0;
    const bool have_f16c = (info[2] & (1 << 29)) != 0;
    const bool have_rdrand = (info[2] & (1 << 30)) != 0;
    const bool have_fma = (info[2] & (1 << 12)) != 0;
    if (have_sse41) {
        available |= (1ULL << halide_target_feature_sse41);
    }
    if (have_avx) {
        available |= (1ULL << halide_target_feature_avx);
    }
    if (have_f16c) {
        available |= (1ULL << halide_target_feature_f16c);
    }
    if (have_fma) {
        available |= (1ULL << halide_target_feature_fma);
    }

    const bool use_64_bits = (sizeof(size_t) == 8);
    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        // So far, so good.  AVX2?
        // Call cpuid with eax=7
        int32_t info2[4];
        cpuid(7, info2);
        const bool have_avx2 = (info2[1] & (1 << 5)) != 0;
        if (have_avx2) {
            available |= (1ULL << halide_target_feature_avx2);
        }
    }
    CpuFeatures features = {known, available};
    return features;
}

}}}  // namespace Halide::Runtime::Internal

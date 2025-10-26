#include "HalideRuntime.h"
#include "cpu_features.h"

namespace Halide {
namespace Runtime {
namespace Internal {

extern "C" void x86_cpuid_halide(int32_t *);
extern "C" void x64_cpuid_halide(int32_t *);

namespace {

constexpr bool use_64_bits = (sizeof(size_t) == 8);

ALWAYS_INLINE void cpuid(int32_t *info, int32_t fn_id, int32_t extra = 0) {
    info[0] = fn_id;
    info[1] = extra;
    if (use_64_bits) {
        x64_cpuid_halide(info);
    } else {
        x86_cpuid_halide(info);
    }
}

}  // namespace

extern "C" WEAK int halide_get_cpu_features(CpuFeatures *features) {
    halide_set_known_cpu_feature(features, halide_target_feature_sse41);
    halide_set_known_cpu_feature(features, halide_target_feature_avx);
    halide_set_known_cpu_feature(features, halide_target_feature_f16c);
    halide_set_known_cpu_feature(features, halide_target_feature_fma);
    halide_set_known_cpu_feature(features, halide_target_feature_avx2);
    halide_set_known_cpu_feature(features, halide_target_feature_avx512);
    halide_set_known_cpu_feature(features, halide_target_feature_avx512_knl);
    halide_set_known_cpu_feature(features, halide_target_feature_avx512_skylake);
    halide_set_known_cpu_feature(features, halide_target_feature_avx512_cannonlake);
    halide_set_known_cpu_feature(features, halide_target_feature_avx512_sapphirerapids);

    // Detect CPU features by specific microarchitecture.
    int32_t vendor[4];
    cpuid(vendor, 0);
    int32_t info[4];
    cpuid(info, 1);

    uint32_t family = (info[0] >> 8) & 0xF;  // Bits 8..11
    uint32_t model = (info[0] >> 4) & 0xF;   // Bits 4..7
    if (family == 0x6 || family == 0xF) {
        if (family == 0xF) {
            // Examine extended family ID if family ID is 0xF.
            family += (info[0] >> 20) & 0xFf;  // Bits 20..27
        }
        // Examine extended model ID if family ID is 0x6 or 0xF.
        model += ((info[0] >> 16) & 0xF) << 4;  // Bits 16..19
    }

    if (vendor[1] == 0x68747541 && vendor[3] == 0x69746e65 && vendor[2] == 0x444d4163) {
        // AMD
        if (family == 0x19 && model == 0x61) {
            // Zen4
            halide_set_available_cpu_feature(features, halide_target_feature_sse41);
            halide_set_available_cpu_feature(features, halide_target_feature_avx);
            halide_set_available_cpu_feature(features, halide_target_feature_f16c);
            halide_set_available_cpu_feature(features, halide_target_feature_fma);
            halide_set_available_cpu_feature(features, halide_target_feature_avx2);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_skylake);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_cannonlake);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_zen4);
            return halide_error_code_success;
        } else if (family == 0x1a) {
            // Zen5
            halide_set_available_cpu_feature(features, halide_target_feature_sse41);
            halide_set_available_cpu_feature(features, halide_target_feature_avx);
            halide_set_available_cpu_feature(features, halide_target_feature_f16c);
            halide_set_available_cpu_feature(features, halide_target_feature_fma);
            halide_set_available_cpu_feature(features, halide_target_feature_avx2);
            halide_set_available_cpu_feature(features, halide_target_feature_avxvnni);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_skylake);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_cannonlake);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_zen4);
            halide_set_available_cpu_feature(features, halide_target_feature_avx512_zen5);
            return halide_error_code_success;
        }
    }

    // Legacy code to detect CPU by feature bits instead. Handle new
    // microarchitectures above rather than making the code below more
    // complicated.

    const bool have_sse41 = (info[2] & (1 << 19)) != 0;
    const bool have_avx = (info[2] & (1 << 28)) != 0;
    const bool have_f16c = (info[2] & (1 << 29)) != 0;
    const bool have_rdrand = (info[2] & (1 << 30)) != 0;
    const bool have_fma = (info[2] & (1 << 12)) != 0;
    if (have_sse41) {
        halide_set_available_cpu_feature(features, halide_target_feature_sse41);
    }
    if (have_avx) {
        halide_set_available_cpu_feature(features, halide_target_feature_avx);
    }
    if (have_f16c) {
        halide_set_available_cpu_feature(features, halide_target_feature_f16c);
    }
    if (have_fma) {
        halide_set_available_cpu_feature(features, halide_target_feature_fma);
    }

    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        int info2[4];
        cpuid(info2, 7);
        int32_t info3[4];
        cpuid(info3, 7, 1);
        constexpr uint32_t avx2 = 1U << 5;
        constexpr uint32_t avx512f = 1U << 16;
        constexpr uint32_t avx512dq = 1U << 17;
        constexpr uint32_t avx512pf = 1U << 26;
        constexpr uint32_t avx512er = 1U << 27;
        constexpr uint32_t avx512cd = 1U << 28;
        constexpr uint32_t avx512bw = 1U << 30;
        constexpr uint32_t avx512vl = 1U << 31;
        constexpr uint32_t avx512ifma = 1U << 21;
        constexpr uint32_t avxvnni = 1U << 4;
        constexpr uint32_t avx512bf16 = 1U << 5;  // bf16 result in eax, cpuid(eax=7, ecx=1)
        constexpr uint32_t avx512 = avx512f | avx512cd;
        constexpr uint32_t avx512_knl = avx512 | avx512pf | avx512er;
        constexpr uint32_t avx512_skylake = avx512 | avx512vl | avx512bw | avx512dq;
        constexpr uint32_t avx512_cannonlake = avx512_skylake | avx512ifma;  // Assume ifma => vbmi
        if ((info2[1] & avx2) == avx2) {
            halide_set_available_cpu_feature(features, halide_target_feature_avx2);
            if ((info3[0] & avxvnni) == avxvnni) {
                halide_set_available_cpu_feature(features, halide_target_feature_avxvnni);
            }
        }
        if ((info2[1] & avx512) == avx512) {
            halide_set_available_cpu_feature(features, halide_target_feature_avx512);
            if ((info2[1] & avx512_knl) == avx512_knl) {
                halide_set_available_cpu_feature(features, halide_target_feature_avx512_knl);
            }
            if ((info2[1] & avx512_skylake) == avx512_skylake) {
                halide_set_available_cpu_feature(features, halide_target_feature_avx512_skylake);
            }
            if ((info2[1] & avx512_cannonlake) == avx512_cannonlake) {
                halide_set_available_cpu_feature(features, halide_target_feature_avx512_cannonlake);

                if ((info3[0] & avx512bf16) == avx512bf16) {
                    halide_set_available_cpu_feature(features, halide_target_feature_avx512_sapphirerapids);
                }
            }
        }
    }
    return halide_error_code_success;
}

}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

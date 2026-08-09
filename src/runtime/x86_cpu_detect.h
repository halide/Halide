/** \file
 *
 * Shared x86 host CPU detection logic. This is used from two very different
 * places:
 *
 *  - libHalide (Target.cpp), to compute the host Target.
 *  - The runtime (x86_cpu_features.cpp), to answer halide_can_use_target_features().
 *
 * The runtime is compiled as freestanding C++17 with no standard library, no
 * exceptions and no RTTI, and it is compiled for a generic target triple, so
 * this header must not:
 *
 *  - include any standard library header (HalideRuntime.h handles both worlds),
 *  - allocate, throw, or call into libc,
 *  - define objects with dynamic initialization,
 *  - execute cpuid/xgetbv itself (the runtime reaches them via hand-written
 *    LLVM IR in x86.ll; libHalide uses intrinsics or inline asm).
 *
 * Everything platform-specific is therefore injected by the caller as an "ops"
 * object. The detection decisions are driven by the values it supplies, which
 * makes the code testable against recorded CPU state from machines we don't
 * have. An ops object must provide:
 *
 *     CpuidResult cpuid(uint32_t leaf, uint32_t subleaf);
 *     uint64_t xgetbv(uint32_t xcr);
 *     void request_amx_os_permission();
 *     void set_feature(halide_target_feature_t f);
 *
 * Facts that aren't target features are returned in X86Detection, so callers
 * that don't care about them can simply ignore them.
 */

#ifndef HALIDE_RUNTIME_X86_CPU_DETECT_H
#define HALIDE_RUNTIME_X86_CPU_DETECT_H

#include "HalideRuntime.h"

namespace Halide {
namespace Internal {
namespace CpuDetect {

struct CpuidResult {
    uint32_t eax, ebx, ecx, edx;
};

enum class X86Vendor {
    Unknown,
    GenuineIntel,
    AuthenticAMD,
};

/** Facts about the host that a caller may want to act on, but that aren't
 * target features. */
struct X86Detection {
    /** Halide's x86 backend assumes SSE2. Callers should report a useful error
     * if this is false; the runtime has no way to complain, so it ignores it. */
    bool have_sse2;

    /** The result of cpuid leaf 1, so that a caller reporting an error about
     * the above can include it. */
    CpuidResult leaf1;

    /** The processor identified from family/model, or generic for other
     * vendors and unrecognized processors. */
    halide_target_processor_t processor;

    /** The maximum vector width reported by AVX10, or zero when unspecified. */
    int vector_bits;
};

/** Every x86 target feature that detect_x86_features() knows how to detect.
 * The runtime uses this to build its mask of "features we know about", which
 * must stay in sync with what we actually set. */
template<typename Fn>
void for_each_detectable_x86_feature(Fn fn) {
    fn(halide_target_feature_sse41);
    fn(halide_target_feature_avx);
    fn(halide_target_feature_avx2);
    fn(halide_target_feature_avxvnni);
    fn(halide_target_feature_fma);
    fn(halide_target_feature_fma4);
    fn(halide_target_feature_f16c);
    fn(halide_target_feature_avx512);
    fn(halide_target_feature_avx512_knl);
    fn(halide_target_feature_avx512_skylake);
    fn(halide_target_feature_avx512_cannonlake);
    fn(halide_target_feature_avx512_zen4);
    fn(halide_target_feature_avx512_zen5);
    fn(halide_target_feature_avx512_sapphirerapids);
    fn(halide_target_feature_avx10_1);
    fn(halide_target_feature_x86_apx);
}

namespace detail {

#if defined(__linux__) || (defined(COMPILING_HALIDE_RUNTIME) && LINUX)
extern "C" long syscall(long number, ...);
#endif

HALIDE_ALWAYS_INLINE void request_linux_amx_permission() {
#if defined(__linux__) || (defined(COMPILING_HALIDE_RUNTIME) && LINUX)
    // On Linux, the AMX XTILEDATA state component (XCR0 bit 18) is allocated
    // lazily: the kernel leaves it disabled in XCR0 for a thread until that
    // thread explicitly requests permission to use it via arch_prctl(2). Without
    // this call, xgetbv() would report AMX as OS-disabled even on hardware that
    // fully supports it. This is a no-op (and harmless) if already granted, and
    // only applies in 64-bit mode, since arch_prctl and AMX are both x86-64-only.
    constexpr long sys_arch_prctl = 158;
    constexpr long arch_req_xcomp_perm = 0x1023;
    constexpr long xfeature_xtiledata = 18;
    syscall(sys_arch_prctl, arch_req_xcomp_perm, xfeature_xtiledata);
#endif
}

template<typename Ops>
X86Vendor get_vendor_signature(Ops &ops) {
    const CpuidResult info = ops.cpuid(0, 0);

    if (info.eax < 1) {
        return X86Vendor::Unknown;
    }

    // "Genu ineI ntel"
    if (info.ebx == 0x756e6547 && info.edx == 0x49656e69 && info.ecx == 0x6c65746e) {
        return X86Vendor::GenuineIntel;
    }

    // "Auth enti cAMD"
    if (info.ebx == 0x68747541 && info.edx == 0x69746e65 && info.ecx == 0x444d4163) {
        return X86Vendor::AuthenticAMD;
    }

    return X86Vendor::Unknown;
}

inline void detect_family_and_model(uint32_t info0, uint32_t &family, uint32_t &model) {
    family = (info0 >> 8) & 0xF;  // Bits 8..11
    model = (info0 >> 4) & 0xF;   // Bits 4..7
    if (family == 0x6 || family == 0xF) {
        if (family == 0xF) {
            // Examine extended family ID if family ID is 0xF.
            family += (info0 >> 20) & 0xFf;  // Bits 20..27
        }
        // Examine extended model ID if family ID is 0x6 or 0xF.
        model += ((info0 >> 16) & 0xF) << 4;  // Bits 16..19
    }
}

inline halide_target_processor_t get_processor_from_amd_family_and_model(uint32_t family, uint32_t model, bool have_sse3) {
    switch (family) {
    case 0xF:  // AMD Family 0Fh
        if (have_sse3) {
            return halide_target_processor_k8_sse3;  // Hammer (modern, with SSE3)
        }
        return halide_target_processor_k8;        // Hammer (original, without SSE3)
    case 0x10:                                    // AMD Family 10h
        return halide_target_processor_amdfam10;  // Barcelona
    case 0x14:                                    // AMD Family 14h
        return halide_target_processor_btver1;    // Bobcat
    case 0x15:                                    // AMD Family 15h
        if (model >= 0x60 && model <= 0x7f) {
            return halide_target_processor_bdver4;  // 60h-7Fh: Excavator
        }
        if (model >= 0x30 && model <= 0x3f) {
            return halide_target_processor_bdver3;  // 30h-3Fh: Steamroller
        }
        if ((model >= 0x10 && model <= 0x1f) || model == 0x02) {
            return halide_target_processor_bdver2;  // 02h, 10h-1Fh: Piledriver
        }
        if (model <= 0x0f) {
            return halide_target_processor_bdver1;  // 00h-0Fh: Bulldozer
        }
        break;
    case 0x16:                                  // AMD Family 16h
        return halide_target_processor_btver2;  // Jaguar
    case 0x17:                                  // AMD Family 17h
        if ((model >= 0x30 && model <= 0x3f) || model == 0x71) {
            return halide_target_processor_znver2;  // 30h-3Fh, 71h: Zen2
        }
        if (model <= 0x0f) {
            return halide_target_processor_znver1;  // 00h-0Fh: Zen1
        }
        break;
    case 0x19:  // AMD Family 19h
        if (
            // Zen 3
            (0x50 <= model && model <= 0x5F) ||  // Cezanne
            (0x40 <= model && model <= 0x4F) ||  // Rembrandt
            (0x30 <= model && model <= 0x3F) ||  // Badami
            (0x20 <= model && model <= 0x2F) ||  // Vermeer
            (model <= 0x0F)                      // Chagall, Milan, Genesis
        ) {
            return halide_target_processor_znver3;
        } else if (
            // Zen 4
            (0xA0 <= model && model <= 0xAF) ||  // Genoa, Dragon Range
            (0x78 <= model && model <= 0x7F) ||  // Phoenix 2, Hawk Point 2 (Zen 4c)
            (0x70 <= model && model <= 0x77) ||  // Phoenix, Hawk Point 1
            (0x60 <= model && model <= 0x6F) ||  // Raphael
            (0x10 <= model && model <= 0x1F)     // Storm Peak
        ) {
            return halide_target_processor_znver4;
        }
        break;
    case 0x1a:                                  // AMD Family 1Ah
        return halide_target_processor_znver5;  // Zen5
    default:
        break;  // Unknown AMD CPU.
    }

    return halide_target_processor_generic;
}

}  // namespace detail

/** Detect the x86 features of the machine this is running on, reporting them
 * through ops.set_feature(). See the file comment for the ops interface. */
template<typename Ops>
X86Detection detect_x86_features(Ops &ops) {
    constexpr bool use_64_bits = (sizeof(size_t) == 8);

    const X86Vendor vendor_signature = detail::get_vendor_signature(ops);
    const CpuidResult info = ops.cpuid(1, 0);

    uint32_t family = 0, model = 0;
    detail::detect_family_and_model(info.eax, family, model);

    // Check OS support for AVX/AVX-512 state saving via XSAVE. Even if the CPU
    // supports these features, the OS must enable the corresponding state
    // components in XCR0 or use will fault.
    const bool have_osxsave = (info.ecx & (1 << 27)) != 0;  // ECX[27]
    bool os_avx = false;
    bool os_avx512 = false;
    bool os_apx = false;
    bool os_amx = false;
    if (have_osxsave) {
        if (use_64_bits) {
            ops.request_amx_os_permission();
        }
        const uint64_t xcr0 = ops.xgetbv(0);
        os_avx = (xcr0 & 0x6) == 0x6;                   // XMM (bit 1) + YMM (bit 2)
        os_avx512 = os_avx && ((xcr0 & 0xE0) == 0xE0);  // opmask (5) + ZMM_Hi256 (6) + Hi16_ZMM (7)
        os_apx = (xcr0 & 0x80000) == 0x80000;           // APX extended GPRs (bit 19)
        os_amx = (xcr0 & 0x60000) == 0x60000;           // AMX XTILECFG (17) + XTILEDATA (18)
    }

    const bool have_sse41 = (info.ecx & (1 << 19)) != 0;           // ECX[19]
    const bool have_sse2 = (info.edx & (1 << 26)) != 0;            // EDX[26]
    const bool have_sse3 = (info.ecx & (1 << 0)) != 0;             // ECX[0]
    const bool have_avx = (info.ecx & (1 << 28)) != 0 && os_avx;   // ECX[28], requires OS AVX support
    const bool have_f16c = (info.ecx & (1 << 29)) != 0 && os_avx;  // ECX[29], VEX-encoded
    const bool have_rdrand = (info.ecx & (1 << 30)) != 0;          // ECX[30]
    const bool have_fma = (info.ecx & (1 << 12)) != 0 && os_avx;   // ECX[12], VEX-encoded

    // FMA4 is in CPUID extended leaf 0x80000001, ECX bit 16. It uses
    // VEX-encoded YMM instructions, so requires OS AVX support.
    const CpuidResult info_ext = ops.cpuid(0x80000001, 0);
    const bool have_fma4 = (info_ext.ecx & (1 << 16)) != 0 && os_avx;  // ECX[16], VEX-encoded

    X86Detection detection = {have_sse2, info, halide_target_processor_generic, 0};

    if (vendor_signature == X86Vendor::AuthenticAMD) {
        const halide_target_processor_t processor =
            detail::get_processor_from_amd_family_and_model(family, model, have_sse3);
        detection.processor = processor;

        if (processor == halide_target_processor_znver4) {
            ops.set_feature(halide_target_feature_sse41);
            if (os_avx) {
                ops.set_feature(halide_target_feature_avx);
                ops.set_feature(halide_target_feature_f16c);
                ops.set_feature(halide_target_feature_fma);
                ops.set_feature(halide_target_feature_avx2);
            }
            if (os_avx512) {
                ops.set_feature(halide_target_feature_avx512);
                ops.set_feature(halide_target_feature_avx512_skylake);
                ops.set_feature(halide_target_feature_avx512_cannonlake);
                ops.set_feature(halide_target_feature_avx512_zen4);
            }
            return detection;
        } else if (processor == halide_target_processor_znver5) {
            ops.set_feature(halide_target_feature_sse41);
            if (os_avx) {
                ops.set_feature(halide_target_feature_avx);
                ops.set_feature(halide_target_feature_f16c);
                ops.set_feature(halide_target_feature_fma);
                ops.set_feature(halide_target_feature_avx2);
                ops.set_feature(halide_target_feature_avxvnni);
            }
            if (os_avx512) {
                ops.set_feature(halide_target_feature_avx512);
                ops.set_feature(halide_target_feature_avx512_skylake);
                ops.set_feature(halide_target_feature_avx512_cannonlake);
                ops.set_feature(halide_target_feature_avx512_zen4);
                ops.set_feature(halide_target_feature_avx512_zen5);
            }
            return detection;
        }
    }

    // Processors not specifically detected by model number above use the cpuid
    // feature bits to determine what flags are supported. For future models,
    // detect them explicitly above rather than extending the code below.

    if (have_sse41) {
        ops.set_feature(halide_target_feature_sse41);
    }
    if (have_avx) {
        ops.set_feature(halide_target_feature_avx);
    }
    if (have_f16c) {
        ops.set_feature(halide_target_feature_f16c);
    }
    if (have_fma) {
        ops.set_feature(halide_target_feature_fma);
    }
    if (have_fma4) {
        ops.set_feature(halide_target_feature_fma4);
    }

    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        // So far, so good. AVX2/512?
        const CpuidResult info2 = ops.cpuid(7, 0);
        const CpuidResult info3 = ops.cpuid(7, 1);
        constexpr uint32_t avx2 = 1U << 5;
        constexpr uint32_t avx512f = 1U << 16;
        constexpr uint32_t avx512dq = 1U << 17;
        constexpr uint32_t avx512pf = 1U << 26;
        constexpr uint32_t avx512er = 1U << 27;
        constexpr uint32_t avx512cd = 1U << 28;
        constexpr uint32_t avx512bw = 1U << 30;
        constexpr uint32_t avx512vl = 1U << 31;
        constexpr uint32_t avx512ifma = 1U << 21;
        constexpr uint32_t avx512 = avx512f | avx512cd;
        constexpr uint32_t avx512_knl = avx512 | avx512pf | avx512er;
        constexpr uint32_t avx512_skylake = avx512 | avx512vl | avx512bw | avx512dq;
        constexpr uint32_t avx512_cannonlake = avx512_skylake | avx512ifma;  // Assume ifma => vbmi
        if ((info2.ebx & avx2) == avx2) {
            ops.set_feature(halide_target_feature_avx2);
        }
        if (os_avx512 && (info2.ebx & avx512) == avx512) {
            ops.set_feature(halide_target_feature_avx512);
            // TODO: port to family/model -based detection.
            if ((info2.ebx & avx512_knl) == avx512_knl) {
                ops.set_feature(halide_target_feature_avx512_knl);
            }
            // TODO: port to family/model -based detection.
            if ((info2.ebx & avx512_skylake) == avx512_skylake) {
                ops.set_feature(halide_target_feature_avx512_skylake);
            }
            // TODO: port to family/model -based detection.
            if ((info2.ebx & avx512_cannonlake) == avx512_cannonlake) {
                ops.set_feature(halide_target_feature_avx512_cannonlake);

                constexpr uint32_t avxvnni = 1U << 4;    // avxvnni (note, not avx512vnni) result in eax
                constexpr uint32_t amx_bf16 = 1U << 22;  // amx_bf16 result in edx, with cpuid(eax=7, ecx=0)
                constexpr uint32_t amx_tile = 1U << 24;  // amx_tile result in edx, with cpuid(eax=7, ecx=0)
                constexpr uint32_t amx_int8 = 1U << 25;  // amx_int8 result in edx, with cpuid(eax=7, ecx=0)
                constexpr uint32_t amx = amx_bf16 | amx_tile | amx_int8;
                // TODO: port to family/model -based detection.
                if ((info3.eax & avxvnni) == avxvnni) {
                    ops.set_feature(halide_target_feature_avxvnni);
                    // avx512_sapphirerapids implies AMX instruction support, which
                    // requires the OS to have enabled the AMX XCR0 state components.
                    if ((info2.edx & amx) == amx && os_amx) {
                        ops.set_feature(halide_target_feature_avx512_sapphirerapids);
                    }
                }
            }
        }

        // AVX10 converged vector instructions. AVX10 uses EVEX encoding with
        // opmask registers at all vector widths, so it requires the same OS
        // XSAVE support as AVX-512. The enumeration bit is
        // CPUID.(EAX=7,ECX=1).EDX[19].
        constexpr uint32_t avx10 = 1U << 19;
        if (os_avx512 && (info3.edx & avx10)) {
            const CpuidResult info_avx10 = ops.cpuid(0x24, 0x0);

            // This checks that the AVX10 version is greater than zero. It
            // isn't really needed as for now only one version exists, but the
            // docs indicate bits 0:7 of EBX should be >= 0 so...
            if ((info_avx10.ebx & 0xff) >= 1) {
                ops.set_feature(halide_target_feature_avx10_1);

                constexpr uint32_t avx10_128 = 1U << 16;
                constexpr uint32_t avx10_256 = 1U << 17;
                constexpr uint32_t avx10_512 = 1U << 18;
                // Choose the maximum one that is available.
                if (info_avx10.ebx & avx10_512) {
                    detection.vector_bits = 512;
                } else if (info_avx10.ebx & avx10_256) {
                    detection.vector_bits = 256;
                } else if (info_avx10.ebx & avx10_128) {  // Not clear it is worth turning on AVX10 for this case.
                    detection.vector_bits = 128;
                }
            }
        }

        // APX extended GPRs (R16-R31) require OS support via XSAVE state
        // component 19 (XCR0 bit 19).
        constexpr uint32_t apx = 1U << 21;
        if (os_apx && (info3.edx & apx)) {
            ops.set_feature(halide_target_feature_x86_apx);
        }
    }

    return detection;
}

}  // namespace CpuDetect
}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_RUNTIME_X86_CPU_DETECT_H

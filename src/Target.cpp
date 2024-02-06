#include <array>
#include <iostream>
#include <string>

#include "Target.h"

#include "Debug.h"
#include "DeviceInterface.h"
#include "Error.h"
#include "Util.h"
#include "WasmExecutor.h"

#if defined(__powerpc__) && (defined(__FreeBSD__) || defined(__linux__))
#if defined(__FreeBSD__)
#include <machine/cpu.h>
#include <sys/elf_common.h>
#endif
// This uses elf.h and must be included after "LLVM_Headers.h", which
// uses llvm/support/Elf.h.
#include <sys/auxv.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif  // _MSC_VER

namespace Halide {

using std::string;
using std::vector;

namespace {

#ifdef _MSC_VER
static void cpuid(int info[4], int infoType, int extra) {
    __cpuidex(info, infoType, extra);
}
#else

#if defined(__x86_64__) || defined(__i386__)
// CPU feature detection code taken from ispc
// (https://github.com/ispc/ispc/blob/master/builtins/dispatch.ll)

void cpuid(int info[4], int infoType, int extra) {
    __asm__ __volatile__(
        "cpuid                 \n\t"
        : "=a"(info[0]), "=b"(info[1]), "=c"(info[2]), "=d"(info[3])
        : "0"(infoType), "2"(extra));
}
#endif
#endif

#if defined(__x86_64__) || defined(__i386__) || defined(_MSC_VER)

enum class VendorSignatures {
    Unknown,
    GenuineIntel,
    AuthenticAMD,
};

VendorSignatures get_vendor_signature() {
    int info[4];
    cpuid(info, 0, 0);

    if (info[0] < 1) {
        return VendorSignatures::Unknown;
    }

    // "Genu ineI ntel"
    if (info[1] == 0x756e6547 && info[3] == 0x49656e69 && info[2] == 0x6c65746e) {
        return VendorSignatures::GenuineIntel;
    }

    // "Auth enti cAMD"
    if (info[1] == 0x68747541 && info[3] == 0x69746e65 && info[2] == 0x444d4163) {
        return VendorSignatures::AuthenticAMD;
    }

    return VendorSignatures::Unknown;
}

void detect_family_and_model(int info0, unsigned &family, unsigned &model) {
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

Target::Processor get_amd_processor(unsigned family, unsigned model, bool have_sse3) {
    switch (family) {
    case 0xF:  // AMD Family 0Fh
        if (have_sse3) {
            return Target::Processor::K8_SSE3;  // Hammer (modern, with SSE3)
        }
        return Target::Processor::K8;        // Hammer (original, without SSE3)
    case 0x10:                               // AMD Family 10h
        return Target::Processor::AMDFam10;  // Barcelona
    case 0x14:                               // AMD Family 14h
        return Target::Processor::BtVer1;    // Bobcat
    case 0x15:                               // AMD Family 15h
        if (model >= 0x60 && model <= 0x7f) {
            return Target::Processor::BdVer4;  // 60h-7Fh: Excavator
        }
        if (model >= 0x30 && model <= 0x3f) {
            return Target::Processor::BdVer3;  // 30h-3Fh: Steamroller
        }
        if ((model >= 0x10 && model <= 0x1f) || model == 0x02) {
            return Target::Processor::BdVer2;  // 02h, 10h-1Fh: Piledriver
        }
        if (model <= 0x0f) {
            return Target::Processor::BdVer1;  // 00h-0Fh: Bulldozer
        }
        break;
    case 0x16:                             // AMD Family 16h
        return Target::Processor::BtVer2;  // Jaguar
    case 0x17:                             // AMD Family 17h
        if ((model >= 0x30 && model <= 0x3f) || model == 0x71) {
            return Target::Processor::ZnVer2;  // 30h-3Fh, 71h: Zen2
        }
        if (model <= 0x0f) {
            return Target::Processor::ZnVer1;  // 00h-0Fh: Zen1
        }
        break;
    case 0x19:  // AMD Family 19h
        if ((model & 0xf0) == 0 || model == 0x21) {
            return Target::Processor::ZnVer3;  // 00h-0Fh, 21h: Zen3
        } else if (model == 0x61) {
            return Target::Processor::ZnVer4;  // 61h: Zen4
        }
        break;
    default:
        break;  // Unknown AMD CPU.
    }

    return Target::Processor::ProcessorGeneric;
}

#endif  // defined(__x86_64__) || defined(__i386__) || defined(_MSC_VER)

Target calculate_host_target() {
    Target::OS os = Target::OSUnknown;
#ifdef __linux__
    os = Target::Linux;
#endif
#ifdef _WIN32
    os = Target::Windows;
#endif
#ifdef __APPLE__
    os = Target::OSX;
#endif

    bool use_64_bits = (sizeof(size_t) == 8);
    int bits = use_64_bits ? 64 : 32;
    int vector_bits = 0;
    Target::Processor processor = Target::Processor::ProcessorGeneric;
    std::vector<Target::Feature> initial_features;

#if __riscv
    Target::Arch arch = Target::RISCV;
#else
#if defined(__arm__) || defined(__aarch64__)
    Target::Arch arch = Target::ARM;
#else
#if defined(__powerpc__) && (defined(__FreeBSD__) || defined(__linux__))
    Target::Arch arch = Target::POWERPC;

#if defined(__linux__)
    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
#elif defined(__FreeBSD__)
    unsigned long hwcap, hwcap2;
    elf_aux_info(AT_HWCAP, &hwcap, sizeof(hwcap));
    elf_aux_info(AT_HWCAP2, &hwcap2, sizeof(hwcap2));
#endif
    bool have_altivec = (hwcap & PPC_FEATURE_HAS_ALTIVEC) != 0;
    bool have_vsx = (hwcap & PPC_FEATURE_HAS_VSX) != 0;
    bool arch_2_07 = (hwcap2 & PPC_FEATURE2_ARCH_2_07) != 0;

    user_assert(have_altivec)
        << "The POWERPC backend assumes at least AltiVec support. This machine does not appear to have AltiVec.\n";

    if (have_vsx) initial_features.push_back(Target::VSX);
    if (arch_2_07) initial_features.push_back(Target::POWER_ARCH_2_07);
#else
    Target::Arch arch = Target::X86;

    VendorSignatures vendor_signature = get_vendor_signature();

    int info[4];
    cpuid(info, 1, 0);

    unsigned family = 0, model = 0;
    detect_family_and_model(info[0], family, model);

    bool have_sse41 = (info[2] & (1 << 19)) != 0;   // ECX[19]
    bool have_sse2 = (info[3] & (1 << 26)) != 0;    // EDX[26]
    bool have_sse3 = (info[2] & (1 << 0)) != 0;     // ECX[0]
    bool have_avx = (info[2] & (1 << 28)) != 0;     // ECX[28]
    bool have_f16c = (info[2] & (1 << 29)) != 0;    // ECX[29]
    bool have_rdrand = (info[2] & (1 << 30)) != 0;  // ECX[30]
    bool have_fma = (info[2] & (1 << 12)) != 0;     // ECX[12]

    user_assert(have_sse2)
        << "The x86 backend assumes at least sse2 support. This machine does not appear to have sse2.\n"
        << "cpuid returned: "
        << std::hex << info[0]
        << ", " << info[1]
        << ", " << info[2]
        << ", " << info[3]
        << std::dec << "\n";

    if (vendor_signature == VendorSignatures::AuthenticAMD) {
        processor = get_amd_processor(family, model, have_sse3);

        if (processor == Target::Processor::ZnVer4) {
            Target t{os, arch, bits, processor, initial_features, vector_bits};
            t.set_features({Target::SSE41, Target::AVX,
                            Target::F16C, Target::FMA,
                            Target::AVX2, Target::AVX512,
                            Target::AVX512_Skylake, Target::AVX512_Cannonlake,
                            Target::AVX512_Zen4});
            return t;
        }
    }

    // Processors not specifically detected by model number above use the cpuid
    // feature bits to determine what flags are supported. For future models,
    // detect them explicitly above rather than extending the code below.

    if (have_sse41) {
        initial_features.push_back(Target::SSE41);
    }
    if (have_avx) {
        initial_features.push_back(Target::AVX);
    }
    if (have_f16c) {
        initial_features.push_back(Target::F16C);
    }
    if (have_fma) {
        initial_features.push_back(Target::FMA);
    }

    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        // So far, so good.  AVX2/512?
        // Call cpuid with eax=7, ecx=0
        int info2[4];
        cpuid(info2, 7, 0);
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
        const uint32_t avx512_cannonlake = avx512_skylake | avx512ifma;  // Assume ifma => vbmi
        if ((info2[1] & avx2) == avx2) {
            initial_features.push_back(Target::AVX2);
        }
        if ((info2[1] & avx512) == avx512) {
            initial_features.push_back(Target::AVX512);
            // TODO: port to family/model -based detection.
            if ((info2[1] & avx512_knl) == avx512_knl) {
                initial_features.push_back(Target::AVX512_KNL);
            }
            // TODO: port to family/model -based detection.
            if ((info2[1] & avx512_skylake) == avx512_skylake) {
                initial_features.push_back(Target::AVX512_Skylake);
            }
            // TODO: port to family/model -based detection.
            if ((info2[1] & avx512_cannonlake) == avx512_cannonlake) {
                initial_features.push_back(Target::AVX512_Cannonlake);

                const uint32_t avxvnni = 1U << 4;     // avxvnni (note, not avx512vnni) result in eax
                const uint32_t avx512bf16 = 1U << 5;  // bf16 result in eax, with cpuid(eax=7, ecx=1)
                int info3[4];
                cpuid(info3, 7, 1);
                // TODO: port to family/model -based detection.
                if ((info3[0] & avxvnni) == avxvnni &&
                    (info3[0] & avx512bf16) == avx512bf16) {
                    initial_features.push_back(Target::AVX512_SapphireRapids);
                }
            }
        }
    }
#endif
#endif
#endif

    return {os, arch, bits, processor, initial_features, vector_bits};
}

bool is_using_hexagon(const Target &t) {
    return (t.has_feature(Target::HVX) ||
            t.has_feature(Target::HVX_v62) ||
            t.has_feature(Target::HVX_v65) ||
            t.has_feature(Target::HVX_v66) ||
            t.has_feature(Target::HexagonDma) ||
            t.arch == Target::Hexagon);
}

int get_hvx_lower_bound(const Target &t) {
    if (!is_using_hexagon(t)) {
        return -1;
    }
    if (t.has_feature(Target::HVX_v62)) {
        return 62;
    }
    if (t.has_feature(Target::HVX_v65)) {
        return 65;
    }
    if (t.has_feature(Target::HVX_v66)) {
        return 66;
    }
    return 60;
}

}  // namespace

Target get_host_target() {
    // Calculating the host target isn't slow but it isn't free,
    // and it's pointless to recalculate it every time we (e.g.) parse
    // an arbitrary Target string. It won't ever change, so cache on first
    // use.
    static Target host_target = calculate_host_target();
    return host_target;
}

namespace {

Target::Feature calculate_host_cuda_capability(Target t) {
    const auto *interface = get_device_interface_for_device_api(DeviceAPI::CUDA, t);
    internal_assert(interface->compute_capability);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    internal_assert(err == 0) << "Failed to query cuda compute capability\n";
    int ver = major * 10 + minor;
    if (ver < 30) {
        return Target::FeatureEnd;
    } else if (ver < 32) {
        return Target::CUDACapability30;
    } else if (ver < 35) {
        return Target::CUDACapability32;
    } else if (ver < 50) {
        return Target::CUDACapability35;
    } else if (ver < 61) {
        return Target::CUDACapability50;
    } else if (ver < 70) {
        return Target::CUDACapability61;
    } else if (ver < 75) {
        return Target::CUDACapability70;
    } else if (ver < 80) {
        return Target::CUDACapability75;
    } else if (ver < 86) {
        return Target::CUDACapability80;
    } else {
        return Target::CUDACapability86;
    }
}

Target::Feature get_host_cuda_capability(Target t) {
    static Target::Feature cap = calculate_host_cuda_capability(t);
    return cap;
}

Target::Feature calculate_host_vulkan_capability(Target t) {
    const auto *interface = get_device_interface_for_device_api(DeviceAPI::Vulkan, t);
    internal_assert(interface->compute_capability);
    int major, minor;
    int err = interface->compute_capability(nullptr, &major, &minor);
    internal_assert(err == 0) << "Failed to query vulkan compute capability\n";
    int ver = major * 10 + minor;
    if (ver < 10) {
        return Target::FeatureEnd;
    } else if (ver < 12) {
        return Target::VulkanV10;
    } else if (ver < 13) {
        return Target::VulkanV12;
    } else {
        return Target::VulkanV13;
    }
}

Target::Feature get_host_vulkan_capability(Target t) {
    static Target::Feature cap = calculate_host_vulkan_capability(t);
    return cap;
}

const std::map<std::string, Target::OS> os_name_map = {
    {"os_unknown", Target::OSUnknown},
    {"linux", Target::Linux},
    {"windows", Target::Windows},
    {"osx", Target::OSX},
    {"android", Target::Android},
    {"ios", Target::IOS},
    {"qurt", Target::QuRT},
    {"noos", Target::NoOS},
    {"fuchsia", Target::Fuchsia},
    {"wasmrt", Target::WebAssemblyRuntime}};

bool lookup_os(const std::string &tok, Target::OS &result) {
    auto os_iter = os_name_map.find(tok);
    if (os_iter != os_name_map.end()) {
        result = os_iter->second;
        return true;
    }
    return false;
}

const std::map<std::string, Target::Arch> arch_name_map = {
    {"arch_unknown", Target::ArchUnknown},
    {"x86", Target::X86},
    {"arm", Target::ARM},
    {"powerpc", Target::POWERPC},
    {"hexagon", Target::Hexagon},
    {"wasm", Target::WebAssembly},
    {"riscv", Target::RISCV},
};

bool lookup_arch(const std::string &tok, Target::Arch &result) {
    auto arch_iter = arch_name_map.find(tok);
    if (arch_iter != arch_name_map.end()) {
        result = arch_iter->second;
        return true;
    }
    return false;
}

/// Important design consideration: currently, the string key is
/// effectively identical to the LLVM CPU string, and it would be really really
/// good to keep it that way, so the proper tune_* can be autogenerated easily
/// from the LLVM CPU string (currently, by replacing "-" with "_",
/// and prepending "tune_" prefix)
///
/// Please keep sorted.
const std::map<std::string, Target::Processor> processor_name_map = {
    {"tune_amdfam10", Target::Processor::AMDFam10},
    {"tune_bdver1", Target::Processor::BdVer1},
    {"tune_bdver2", Target::Processor::BdVer2},
    {"tune_bdver3", Target::Processor::BdVer3},
    {"tune_bdver4", Target::Processor::BdVer4},
    {"tune_btver1", Target::Processor::BtVer1},
    {"tune_btver2", Target::Processor::BtVer2},
    {"tune_generic", Target::Processor::ProcessorGeneric},
    {"tune_k8", Target::Processor::K8},
    {"tune_k8_sse3", Target::Processor::K8_SSE3},
    {"tune_znver1", Target::Processor::ZnVer1},
    {"tune_znver2", Target::Processor::ZnVer2},
    {"tune_znver3", Target::Processor::ZnVer3},
    {"tune_znver4", Target::Processor::ZnVer4},
};

bool lookup_processor(const std::string &tok, Target::Processor &result) {
    auto processor_iter = processor_name_map.find(tok);
    if (processor_iter != processor_name_map.end()) {
        result = processor_iter->second;
        return true;
    }
    return false;
}

const std::map<std::string, Target::Feature> feature_name_map = {
    {"jit", Target::JIT},
    {"debug", Target::Debug},
    {"no_asserts", Target::NoAsserts},
    {"no_bounds_query", Target::NoBoundsQuery},
    {"sse41", Target::SSE41},
    {"avx", Target::AVX},
    {"avx2", Target::AVX2},
    {"fma", Target::FMA},
    {"fma4", Target::FMA4},
    {"f16c", Target::F16C},
    {"armv7s", Target::ARMv7s},
    {"no_neon", Target::NoNEON},
    {"vsx", Target::VSX},
    {"power_arch_2_07", Target::POWER_ARCH_2_07},
    {"cuda", Target::CUDA},
    {"cuda_capability_30", Target::CUDACapability30},
    {"cuda_capability_32", Target::CUDACapability32},
    {"cuda_capability_35", Target::CUDACapability35},
    {"cuda_capability_50", Target::CUDACapability50},
    {"cuda_capability_61", Target::CUDACapability61},
    {"cuda_capability_70", Target::CUDACapability70},
    {"cuda_capability_75", Target::CUDACapability75},
    {"cuda_capability_80", Target::CUDACapability80},
    {"cuda_capability_86", Target::CUDACapability86},
    {"opencl", Target::OpenCL},
    {"cl_doubles", Target::CLDoubles},
    {"cl_half", Target::CLHalf},
    {"cl_atomics64", Target::CLAtomics64},
    {"openglcompute", Target::OpenGLCompute},
    {"egl", Target::EGL},
    {"user_context", Target::UserContext},
    {"profile", Target::Profile},
    {"no_runtime", Target::NoRuntime},
    {"metal", Target::Metal},
    {"c_plus_plus_name_mangling", Target::CPlusPlusMangling},
    {"large_buffers", Target::LargeBuffers},
    {"hvx", Target::HVX_128},
    {"hvx_128", Target::HVX_128},
    {"hvx_v62", Target::HVX_v62},
    {"hvx_v65", Target::HVX_v65},
    {"hvx_v66", Target::HVX_v66},
    {"fuzz_float_stores", Target::FuzzFloatStores},
    {"soft_float_abi", Target::SoftFloatABI},
    {"msan", Target::MSAN},
    {"avx512", Target::AVX512},
    {"avx512_knl", Target::AVX512_KNL},
    {"avx512_skylake", Target::AVX512_Skylake},
    {"avx512_cannonlake", Target::AVX512_Cannonlake},
    {"avx512_sapphirerapids", Target::AVX512_SapphireRapids},
    {"avx512_zen4", Target::AVX512_Zen4},
    {"trace_loads", Target::TraceLoads},
    {"trace_stores", Target::TraceStores},
    {"trace_realizations", Target::TraceRealizations},
    {"trace_pipeline", Target::TracePipeline},
    {"d3d12compute", Target::D3D12Compute},
    {"strict_float", Target::StrictFloat},
    {"tsan", Target::TSAN},
    {"asan", Target::ASAN},
    {"check_unsafe_promises", Target::CheckUnsafePromises},
    {"hexagon_dma", Target::HexagonDma},
    {"embed_bitcode", Target::EmbedBitcode},
    {"enable_llvm_loop_opt", Target::EnableLLVMLoopOpt},
    {"wasm_simd128", Target::WasmSimd128},
    {"wasm_mvponly", Target::WasmMvpOnly},
    {"wasm_threads", Target::WasmThreads},
    {"wasm_bulk_memory", Target::WasmBulkMemory},
    {"webgpu", Target::WebGPU},
    {"sve", Target::SVE},
    {"sve2", Target::SVE2},
    {"arm_dot_prod", Target::ARMDotProd},
    {"arm_fp16", Target::ARMFp16},
    {"llvm_large_code_model", Target::LLVMLargeCodeModel},
    {"rvv", Target::RVV},
    {"armv81a", Target::ARMv81a},
    {"sanitizer_coverage", Target::SanitizerCoverage},
    {"profile_by_timer", Target::ProfileByTimer},
    {"spirv", Target::SPIRV},
    {"vulkan", Target::Vulkan},
    {"vk_int8", Target::VulkanInt8},
    {"vk_int16", Target::VulkanInt16},
    {"vk_int64", Target::VulkanInt64},
    {"vk_float16", Target::VulkanFloat16},
    {"vk_float64", Target::VulkanFloat64},
    {"vk_v10", Target::VulkanV10},
    {"vk_v12", Target::VulkanV12},
    {"vk_v13", Target::VulkanV13},
    {"semihosting", Target::Semihosting},
    // NOTE: When adding features to this map, be sure to update PyEnums.cpp as well.
};

bool lookup_feature(const std::string &tok, Target::Feature &result) {
    auto feature_iter = feature_name_map.find(tok);
    if (feature_iter != feature_name_map.end()) {
        result = feature_iter->second;
        return true;
    }
    return false;
}

int parse_vector_bits(const std::string &tok) {
    if (tok.find("vector_bits_") == 0) {
        std::string num = tok.substr(sizeof("vector_bits_") - 1, std::string::npos);
        size_t end_index;
        int parsed = std::stoi(num, &end_index);
        if (end_index == num.size()) {
            return parsed;
        }
    }
    return -1;
}

void set_sanitizer_bits(Target &t) {
// Note, we must include Util.h for these to be defined properly (or not)
#ifdef HALIDE_INTERNAL_USING_ASAN
    t.set_feature(Target::ASAN);
#endif
#ifdef HALIDE_INTERNAL_USING_MSAN
    t.set_feature(Target::MSAN);
#endif
#ifdef HALIDE_INTERNAL_USING_TSAN
    t.set_feature(Target::TSAN);
#endif
#ifdef HALIDE_INTERNAL_USING_COVSAN
    t.set_feature(Target::SanitizerCoverage);
#endif
}

}  // End anonymous namespace

Target get_target_from_environment() {
    string target = Internal::get_env_variable("HL_TARGET");
    if (target.empty()) {
        return get_host_target();
    } else {
        return Target(target);
    }
}

Target get_jit_target_from_environment() {
    Target host = get_host_target();
    host.set_feature(Target::JIT);

    string target = Internal::get_env_variable("HL_JIT_TARGET");
    if (target.empty()) {
        set_sanitizer_bits(host);
        return host;
    } else {
        Target t(target);
        t.set_feature(Target::JIT);
        user_assert((t.os == host.os && t.arch == host.arch && t.bits == host.bits) || Internal::WasmModule::can_jit_target(t))
            << "HL_JIT_TARGET must match the host OS, architecture, and bit width.\n"
            << "HL_JIT_TARGET was " << target << ". "
            << "Host is " << host.to_string() << ".\n";
        user_assert(!t.has_feature(Target::NoBoundsQuery))
            << "The Halide JIT requires the use of bounds query, but HL_JIT_TARGET was specified with no_bounds_query: " << target;
        set_sanitizer_bits(t);
        return t;
    }
}

namespace {
bool merge_string(Target &t, const std::string &target) {
    string rest = target;
    vector<string> tokens;
    size_t first_dash;
    while ((first_dash = rest.find('-')) != string::npos) {
        // Internal::debug(0) << first_dash << ", " << rest << "\n";
        tokens.push_back(rest.substr(0, first_dash));
        rest = rest.substr(first_dash + 1);
    }
    tokens.push_back(rest);

    bool os_specified = false, arch_specified = false, bits_specified = false, processor_specified = false, features_specified = false;
    bool is_host = false;

    for (size_t i = 0; i < tokens.size(); i++) {
        const string &tok = tokens[i];
        Target::Feature feature;
        int vector_bits;

        if (tok == "host") {
            if (i > 0) {
                // "host" is now only allowed as the first token.
                return false;
            }
            is_host = true;
            t = get_host_target();
        } else if (tok == "32" || tok == "64" || tok == "0") {
            if (bits_specified) {
                return false;
            }
            bits_specified = true;
            t.bits = std::stoi(tok);
        } else if (lookup_arch(tok, t.arch)) {
            if (arch_specified) {
                return false;
            }
            arch_specified = true;
        } else if (lookup_os(tok, t.os)) {
            if (os_specified) {
                return false;
            }
            os_specified = true;
        } else if (lookup_processor(tok, t.processor_tune)) {
            if (processor_specified) {
                return false;
            }
            processor_specified = true;
        } else if (lookup_feature(tok, feature)) {
            t.set_feature(feature);
            features_specified = true;
        } else if (tok == "trace_all") {
            t.set_features({Target::TraceLoads, Target::TraceStores, Target::TraceRealizations});
            features_specified = true;
        } else if ((vector_bits = parse_vector_bits(tok)) >= 0) {
            t.vector_bits = vector_bits;
        } else {
            return false;
        }
    }

    if (is_host &&
        t.has_feature(Target::CUDA) &&
        !t.has_feature(Target::CUDACapability30) &&
        !t.has_feature(Target::CUDACapability32) &&
        !t.has_feature(Target::CUDACapability35) &&
        !t.has_feature(Target::CUDACapability50) &&
        !t.has_feature(Target::CUDACapability61) &&
        !t.has_feature(Target::CUDACapability70) &&
        !t.has_feature(Target::CUDACapability75) &&
        !t.has_feature(Target::CUDACapability80) &&
        !t.has_feature(Target::CUDACapability86)) {
        // Detect host cuda capability
        t.set_feature(get_host_cuda_capability(t));
    }

    if (is_host &&
        t.has_feature(Target::Vulkan) &&
        !t.has_feature(Target::VulkanV10) &&
        !t.has_feature(Target::VulkanV12) &&
        !t.has_feature(Target::VulkanV13)) {
        // Detect host vulkan capability
        t.set_feature(get_host_vulkan_capability(t));
    }

    if (arch_specified && !bits_specified) {
        return false;
    }

    if (bits_specified && t.bits == 0) {
        // bits == 0 is allowed iff arch and os are "unknown" and no features are set,
        // to allow for roundtripping the string for default Target() ctor.
        if (!(arch_specified && t.arch == Target::ArchUnknown) ||
            !(os_specified && t.os == Target::OSUnknown) ||
            features_specified) {
            return false;
        }
    }

    return true;
}

void bad_target_string(const std::string &target) {
    const char *separator = "";
    std::string architectures;
    for (const auto &arch_entry : arch_name_map) {
        architectures += separator + arch_entry.first;
        separator = ", ";
    }
    separator = "";
    std::string oses;
    for (const auto &os_entry : os_name_map) {
        oses += separator + os_entry.first;
        separator = ", ";
    }
    separator = "";
    std::string processors;
    for (const auto &processor_entry : processor_name_map) {
        processors += separator + processor_entry.first;
        separator = ", ";
    }
    separator = "";
    // Format the features to go one feature over 70 characters per line,
    // assume the first line starts with "Features are ".
    int line_char_start = -(int)sizeof("Features are");
    std::string features;
    for (const auto &feature_entry : feature_name_map) {
        features += separator + feature_entry.first;
        if (features.length() - line_char_start > 70) {
            separator = "\n";
            line_char_start = features.length();
        } else {
            separator = ", ";
        }
    }
    user_error << "Did not understand Halide target " << target << "\n"
               << "Expected format is arch-bits-os-processor-feature1-feature2-...\n"
               << "Where arch is: " << architectures << ".\n"
               << "bits is either 32 or 64.\n"
               << "os is: " << oses << ".\n"
               << "processor is: " << processors << ".\n"
               << "\n"
               << "If arch, bits, or os are omitted, they default to the host.\n"
               << "\n"
               << "If processor is omitted, it defaults to tune_generic.\n"
               << "\n"
               << "Features are: " << features << ".\n"
               << "\n"
               << "The target can also begin with \"host\", which sets the "
               << "host's architecture, os, and feature set, with the "
               << "exception of the GPU runtimes, which default to off.\n"
               << "\n"
               << "On this platform, the host target is: " << get_host_target().to_string() << "\n";
}

void do_check_bad(const Target &t, const std::initializer_list<Target::Feature> &v) {
    for (Target::Feature f : v) {
        user_assert(!t.has_feature(f))
            << "Target feature " << Target::feature_to_name(f)
            << " is incompatible with the Target's architecture. (" << t << ")\n";
    }
}

}  // namespace

void Target::validate_features() const {
    // Note that the features don't have to be exhaustive, but enough to avoid obvious mistakes is good.
    if (arch == X86) {
        do_check_bad(*this, {
                                ARMDotProd,
                                ARMFp16,
                                ARMv7s,
                                ARMv81a,
                                NoNEON,
                                POWER_ARCH_2_07,
                                RVV,
                                SVE,
                                SVE2,
                                VSX,
                                WasmBulkMemory,
                                WasmMvpOnly,
                                WasmSimd128,
                                WasmThreads,
                            });
    } else if (arch == ARM) {
        do_check_bad(*this, {
                                AVX,
                                AVX2,
                                AVX512,
                                AVX512_Cannonlake,
                                AVX512_KNL,
                                AVX512_SapphireRapids,
                                AVX512_Skylake,
                                AVX512_Zen4,
                                F16C,
                                FMA,
                                FMA4,
                                POWER_ARCH_2_07,
                                RVV,
                                SSE41,
                                VSX,
                                WasmBulkMemory,
                                WasmMvpOnly,
                                WasmSimd128,
                                WasmThreads,
                            });
    } else if (arch == WebAssembly) {
        do_check_bad(*this, {
                                ARMDotProd,
                                ARMFp16,
                                ARMv7s,
                                ARMv81a,
                                AVX,
                                AVX2,
                                AVX512,
                                AVX512_Cannonlake,
                                AVX512_KNL,
                                AVX512_SapphireRapids,
                                AVX512_Skylake,
                                AVX512_Zen4,
                                F16C,
                                FMA,
                                FMA4,
                                HVX_128,
                                HVX_128,
                                HVX_v62,
                                HVX_v65,
                                HVX_v66,
                                NoNEON,
                                POWER_ARCH_2_07,
                                RVV,
                                SSE41,
                                SVE,
                                SVE2,
                                VSX,
                            });
    }
}

Target::Target(const std::string &target) {
    Target host = get_host_target();

    if (target.empty()) {
        // If nothing is specified, use the full host target.
        *this = host;
    } else {
        if (!merge_string(*this, target) || has_unknowns()) {
            bad_target_string(target);
        }
    }
    validate_features();
}

Target::Target(const char *s)
    : Target(std::string(s)) {
}

bool Target::validate_target_string(const std::string &s) {
    Target t;
    return merge_string(t, s) && !t.has_unknowns();
}

std::string Target::feature_to_name(Target::Feature feature) {
    for (const auto &feature_entry : feature_name_map) {
        if (feature == feature_entry.second) {
            return feature_entry.first;
        }
    }
    internal_error;
    return "";
}

Target::Feature Target::feature_from_name(const std::string &name) {
    Target::Feature feature;
    if (lookup_feature(name, feature)) {
        return feature;
    }
    return Target::FeatureEnd;
}

std::string Target::to_string() const {
    string result;
    for (const auto &arch_entry : arch_name_map) {
        if (arch_entry.second == arch) {
            result += arch_entry.first;
            break;
        }
    }
    result += "-" + std::to_string(bits);
    for (const auto &os_entry : os_name_map) {
        if (os_entry.second == os) {
            result += "-" + os_entry.first;
            break;
        }
    }
    if (processor_tune != ProcessorGeneric) {
        for (const auto &processor_entry : processor_name_map) {
            if (processor_entry.second == processor_tune) {
                result += "-" + processor_entry.first;
                break;
            }
        }
    }
    for (const auto &feature_entry : feature_name_map) {
        if (has_feature(feature_entry.second)) {
            result += "-" + feature_entry.first;
        }
    }
    // Use has_feature() multiple times (rather than features_any_of())
    // to avoid constructing a temporary vector for this rather-common call.
    if (has_feature(Target::TraceLoads) && has_feature(Target::TraceStores) && has_feature(Target::TraceRealizations)) {
        result = Internal::replace_all(result, "trace_loads-trace_realizations-trace_stores", "trace_all");
    }
    if (vector_bits != 0) {
        result += "-vector_bits_" + std::to_string(vector_bits);
    }

    return result;
}

/** Was libHalide compiled with support for this target? */
bool Target::supported() const {
    bool bad = false;
#if !defined(WITH_ARM)
    bad |= arch == Target::ARM && bits == 32;
#endif
#if !defined(WITH_AARCH64)
    bad |= arch == Target::ARM && bits == 64;
#endif
#if !defined(WITH_X86)
    bad |= arch == Target::X86;
#endif
#if !defined(WITH_POWERPC)
    bad |= arch == Target::POWERPC;
#endif
#if !defined(WITH_HEXAGON)
    bad |= arch == Target::Hexagon;
#endif
#if !defined(WITH_WEBASSEMBLY)
    bad |= arch == Target::WebAssembly;
#endif
#if !defined(WITH_RISCV)
    bad |= arch == Target::RISCV;
#endif
#if !defined(WITH_NVPTX)
    bad |= has_feature(Target::CUDA);
#endif
#if !defined(WITH_OPENCL)
    bad |= has_feature(Target::OpenCL);
#endif
#if !defined(WITH_METAL)
    bad |= has_feature(Target::Metal);
#endif
#if !defined(WITH_OPENGLCOMPUTE)
    bad |= has_feature(Target::OpenGLCompute);
#endif
#if !defined(WITH_D3D12)
    bad |= has_feature(Target::D3D12Compute);
#endif
#if !defined(WITH_VULKAN)
    bad |= has_feature(Target::Vulkan);
#endif
#if !defined(WITH_WEBGPU)
    bad |= has_feature(Target::WebGPU);
#endif
    return !bad;
}

bool Target::has_unknowns() const {
    return os == OSUnknown || arch == ArchUnknown || bits == 0;
}

void Target::set_feature(Feature f, bool value) {
    if (f == FeatureEnd) {
        return;
    }
    user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
    features.set(f, value);
}

void Target::set_features(const std::vector<Feature> &features_to_set, bool value) {
    for (Feature f : features_to_set) {
        set_feature(f, value);
    }
}

bool Target::has_feature(Feature f) const {
    if (f == FeatureEnd) {
        return true;
    }
    user_assert(f < FeatureEnd) << "Invalid Target feature.\n";
    return features[f];
}

bool Target::features_any_of(const std::vector<Feature> &test_features) const {
    for (Feature f : test_features) {
        if (has_feature(f)) {
            return true;
        }
    }
    return false;
}

bool Target::features_all_of(const std::vector<Feature> &test_features) const {
    for (Feature f : test_features) {
        if (!has_feature(f)) {
            return false;
        }
    }
    return true;
}

Target Target::with_feature(Feature f) const {
    Target copy = *this;
    copy.set_feature(f);
    return copy;
}

Target Target::without_feature(Feature f) const {
    Target copy = *this;
    copy.set_feature(f, false);
    return copy;
}

bool Target::has_gpu_feature() const {
    return (has_feature(CUDA) ||
            has_feature(OpenCL) ||
            has_feature(Metal) ||
            has_feature(D3D12Compute) ||
            has_feature(OpenGLCompute) ||
            has_feature(Vulkan) ||
            has_feature(WebGPU));
}

int Target::get_cuda_capability_lower_bound() const {
    if (!has_feature(Target::CUDA)) {
        return -1;
    }
    if (has_feature(Target::CUDACapability30)) {
        return 30;
    }
    if (has_feature(Target::CUDACapability32)) {
        return 32;
    }
    if (has_feature(Target::CUDACapability35)) {
        return 35;
    }
    if (has_feature(Target::CUDACapability50)) {
        return 50;
    }
    if (has_feature(Target::CUDACapability61)) {
        return 61;
    }
    if (has_feature(Target::CUDACapability70)) {
        return 70;
    }
    if (has_feature(Target::CUDACapability75)) {
        return 75;
    }
    if (has_feature(Target::CUDACapability80)) {
        return 80;
    }
    if (has_feature(Target::CUDACapability86)) {
        return 86;
    }
    return 20;
}

int Target::get_vulkan_capability_lower_bound() const {
    if (!has_feature(Target::Vulkan)) {
        return -1;
    }
    if (has_feature(Target::VulkanV10)) {
        return 10;
    }
    if (has_feature(Target::VulkanV12)) {
        return 12;
    }
    if (has_feature(Target::VulkanV13)) {
        return 13;
    }
    return 10;
}

bool Target::supports_type(const Type &t) const {
    if (t.bits() == 64) {
        if (t.is_float()) {
            return (!has_feature(Metal) &&
                    !has_feature(OpenGLCompute) &&
                    !has_feature(D3D12Compute) &&
                    (!has_feature(Target::OpenCL) || has_feature(Target::CLDoubles)) &&
                    (!has_feature(Vulkan) || has_feature(Target::VulkanFloat64)) &&
                    !has_feature(WebGPU));
        } else {
            return (!has_feature(Metal) &&
                    !has_feature(OpenGLCompute) &&
                    !has_feature(D3D12Compute) &&
                    (!has_feature(Vulkan) || has_feature(Target::VulkanInt64)) &&
                    !has_feature(WebGPU));
        }
    }
    return true;
}

bool Target::supports_type(const Type &t, DeviceAPI device) const {
    if (device == DeviceAPI::Default_GPU) {
        device = get_default_device_api_for_target(*this);
    }

    if (device == DeviceAPI::Hexagon) {
        // HVX supports doubles and long long in the scalar unit only.
        if (t.is_float() || t.bits() == 64) {
            return t.lanes() == 1;
        }
    } else if (device == DeviceAPI::Metal) {
        // Metal spec says no double or long long.
        if (t.bits() == 64) {
            return false;
        }
    } else if (device == DeviceAPI::OpenCL) {
        if (t.is_float() && t.bits() == 64) {
            return has_feature(Target::CLDoubles);
        }
    } else if (device == DeviceAPI::D3D12Compute) {
        // Shader Model 5.x can optionally support double-precision; 64-bit int
        // types are not supported.
        return t.bits() < 64;
    } else if (device == DeviceAPI::OpenGLCompute) {
        return t.bits() < 64;
    } else if (device == DeviceAPI::Vulkan) {
        if (t.is_float() && t.bits() == 64) {
            return has_feature(Target::VulkanFloat64);
        } else if (t.is_float() && t.bits() == 16) {
            return has_feature(Target::VulkanFloat16);
        } else if (t.is_int_or_uint() && t.bits() == 64) {
            return has_feature(Target::VulkanInt64);
        } else if (t.is_int_or_uint() && t.bits() == 16) {
            return has_feature(Target::VulkanInt16);
        } else if (t.is_int_or_uint() && t.bits() == 8) {
            return has_feature(Target::VulkanInt8);
        }
    } else if (device == DeviceAPI::WebGPU) {
        return t.bits() < 64;
    }

    return true;
}

bool Target::supports_device_api(DeviceAPI api) const {
    switch (api) {
    case DeviceAPI::None:
        return true;
    case DeviceAPI::Host:
        return true;
    case DeviceAPI::Default_GPU:
        return has_gpu_feature();
    case DeviceAPI::Hexagon:
        return has_feature(Target::HVX);
    case DeviceAPI::HexagonDma:
        return has_feature(Target::HexagonDma);
    default:
        return has_feature(target_feature_for_device_api(api));
    }
}

DeviceAPI Target::get_required_device_api() const {
    if (has_feature(Target::CUDA)) {
        return DeviceAPI::CUDA;
    }
    if (has_feature(Target::D3D12Compute)) {
        return DeviceAPI::D3D12Compute;
    }
    if (has_feature(Target::HVX)) {
        return DeviceAPI::Hexagon;
    }
    if (has_feature(Target::HexagonDma)) {
        return DeviceAPI::HexagonDma;
    }
    if (has_feature(Target::Metal)) {
        return DeviceAPI::Metal;
    }
    if (has_feature(Target::OpenCL)) {
        return DeviceAPI::OpenCL;
    }
    if (has_feature(Target::OpenGLCompute)) {
        return DeviceAPI::OpenGLCompute;
    }
    if (has_feature(Target::Vulkan)) {
        return DeviceAPI::Vulkan;
    }
    if (has_feature(Target::WebGPU)) {
        return DeviceAPI::WebGPU;
    }
    return DeviceAPI::None;
}

Target::Feature target_feature_for_device_api(DeviceAPI api) {
    switch (api) {
    case DeviceAPI::CUDA:
        return Target::CUDA;
    case DeviceAPI::OpenCL:
        return Target::OpenCL;
    case DeviceAPI::OpenGLCompute:
        return Target::OpenGLCompute;
    case DeviceAPI::Metal:
        return Target::Metal;
    case DeviceAPI::Hexagon:
        return Target::HVX;
    case DeviceAPI::D3D12Compute:
        return Target::D3D12Compute;
    case DeviceAPI::Vulkan:
        return Target::Vulkan;
    case DeviceAPI::WebGPU:
        return Target::WebGPU;
    default:
        return Target::FeatureEnd;
    }
}

int Target::natural_vector_size(const Halide::Type &t) const {
    user_assert(!has_unknowns())
        << "natural_vector_size cannot be used on a Target with Unknown values.\n";

    const bool is_integer = t.is_int() || t.is_uint();
    const int data_size = t.bytes();

    if (arch == Target::ARM) {
        if (vector_bits != 0 &&
            (has_feature(Halide::Target::SVE2) ||
             (t.is_float() && has_feature(Halide::Target::SVE)))) {
            return vector_bits / (data_size * 8);
        } else {
            return 16 / data_size;
        }
    } else if (arch == Target::Hexagon) {
        if (is_integer) {
            if (has_feature(Halide::Target::HVX)) {
                return 128 / data_size;
            } else {
                user_error << "Target uses hexagon arch without target feature hvx set.\n";
                return 0;
            }
        } else {
            // HVX does not have vector float instructions.
            return 1;
        }
    } else if (arch == Target::X86) {
        if (is_integer && (has_feature(Halide::Target::AVX512_Skylake) ||
                           has_feature(Halide::Target::AVX512_Cannonlake))) {
            // AVX512BW exists on Skylake and Cannonlake
            return 64 / data_size;
        } else if (t.is_float() && (has_feature(Halide::Target::AVX512) ||
                                    has_feature(Halide::Target::AVX512_KNL) ||
                                    has_feature(Halide::Target::AVX512_Skylake) ||
                                    has_feature(Halide::Target::AVX512_Cannonlake))) {
            // AVX512F is on all AVX512 architectures
            return 64 / data_size;
        } else if (has_feature(Halide::Target::AVX2)) {
            // AVX2 uses 256-bit vectors for everything.
            return 32 / data_size;
        } else if (!is_integer && has_feature(Halide::Target::AVX)) {
            // AVX 1 has 256-bit vectors for float, but not for
            // integer instructions.
            return 32 / data_size;
        } else {
            // SSE was all 128-bit. We ignore MMX.
            return 16 / data_size;
        }
    } else if (arch == Target::WebAssembly) {
        if (has_feature(Halide::Target::WasmSimd128)) {
            // 128-bit vectors for other types.
            return 16 / data_size;
        } else {
            // No vectors, sorry.
            return 1;
        }
    } else if (arch == Target::RISCV) {
        if (vector_bits != 0 &&
            has_feature(Halide::Target::RVV)) {
            return vector_bits / (data_size * 8);
        } else {
            return 1;
        }
    } else {
        // Assume 128-bit vectors on other targets.
        return 16 / data_size;
    }
}

bool Target::get_runtime_compatible_target(const Target &other, Target &result) {
    // Create mask to select features that:
    // (a) must be included if either target has the feature (union)
    // (b) must be included if both targets have the feature (intersection)
    // (c) must match across both targets; it is an error if one target has the feature and the other doesn't

    // clang-format off
    const std::array<Feature, 23> union_features = {{
        // These are true union features.
        CUDA,
        D3D12Compute,
        Metal,
        NoNEON,
        OpenCL,
        OpenGLCompute,
        Vulkan,
        WebGPU,

        // These features are actually intersection-y, but because targets only record the _highest_,
        // we have to put their union in the result and then take a lower bound.
        CUDACapability30,
        CUDACapability32,
        CUDACapability35,
        CUDACapability50,
        CUDACapability61,
        CUDACapability70,
        CUDACapability75,
        CUDACapability80,
        CUDACapability86,
        HVX_v62,
        HVX_v65,
        HVX_v66,
        VulkanV10,
        VulkanV12,
        VulkanV13,
    }};
    // clang-format on

    // clang-format off
    const std::array<Feature, 15> intersection_features = {{
        ARMv7s,
        ARMv81a,
        AVX,
        AVX2,
        AVX512,
        AVX512_Cannonlake,
        AVX512_KNL,
        AVX512_SapphireRapids,
        AVX512_Skylake,
        AVX512_Zen4,
        F16C,
        FMA,
        FMA4,
        SSE41,
        VSX,
    }};
    // clang-format on

    // clang-format off
    const std::array<Feature, 10> matching_features = {{
        ASAN,
        Debug,
        HexagonDma,
        HVX,
        MSAN,
        SoftFloatABI,
        TSAN,
        WasmThreads,
        SanitizerCoverage,
    }};
    // clang-format on

    // bitsets need to be the same width.
    decltype(result.features) union_mask;
    decltype(result.features) intersection_mask;
    decltype(result.features) matching_mask;

    for (const auto &feature : union_features) {
        union_mask.set(feature);
    }

    for (const auto &feature : intersection_features) {
        intersection_mask.set(feature);
    }

    for (const auto &feature : matching_features) {
        matching_mask.set(feature);
    }

    if (arch != other.arch || bits != other.bits || os != other.os) {
        Internal::debug(1) << "runtime targets must agree on platform (arch-bits-os)\n"
                           << "  this:  " << *this << "\n"
                           << "  other: " << other << "\n";
        return false;
    }

    if ((features & matching_mask) != (other.features & matching_mask)) {
        Internal::debug(1) << "runtime targets must agree on SoftFloatABI, Debug, TSAN, ASAN, MSAN, HVX, HexagonDma, SanitizerCoverage\n"
                           << "  this:  " << *this << "\n"
                           << "  other: " << other << "\n";
        return false;
    }

    // Union of features is computed through bitwise-or, and masked away by the features we care about
    // Intersection of features is computed through bitwise-and and masked away, too.
    // We merge the bits via bitwise or.
    Target output = Target{os, arch, bits, processor_tune};
    output.features = ((features | other.features) & union_mask) | ((features | other.features) & matching_mask) | ((features & other.features) & intersection_mask);

    // Pick tight lower bound for CUDA capability. Use fall-through to clear redundant features
    int cuda_a = get_cuda_capability_lower_bound();
    int cuda_b = other.get_cuda_capability_lower_bound();

    // get_cuda_capability_lower_bound returns -1 when unused. Casting to unsigned makes this
    // large, so min selects the true lower bound when one target doesn't specify a capability,
    // and the other doesn't use CUDA at all.
    int cuda_capability = std::min((unsigned)cuda_a, (unsigned)cuda_b);
    if (cuda_capability < 30) {
        output.features.reset(CUDACapability30);
    }
    if (cuda_capability < 32) {
        output.features.reset(CUDACapability32);
    }
    if (cuda_capability < 35) {
        output.features.reset(CUDACapability35);
    }
    if (cuda_capability < 50) {
        output.features.reset(CUDACapability50);
    }
    if (cuda_capability < 61) {
        output.features.reset(CUDACapability61);
    }
    if (cuda_capability < 70) {
        output.features.reset(CUDACapability70);
    }
    if (cuda_capability < 75) {
        output.features.reset(CUDACapability75);
    }
    if (cuda_capability < 80) {
        output.features.reset(CUDACapability80);
    }
    if (cuda_capability < 86) {
        output.features.reset(CUDACapability86);
    }

    // Pick tight lower bound for Vulkan capability. Use fall-through to clear redundant features
    int vulkan_a = get_vulkan_capability_lower_bound();
    int vulkan_b = other.get_vulkan_capability_lower_bound();

    // Same trick as above for CUDA
    int vulkan_capability = std::min((unsigned)vulkan_a, (unsigned)vulkan_b);
    if (vulkan_capability < 10) {
        output.features.reset(VulkanV10);
    }
    if (vulkan_capability < 12) {
        output.features.reset(VulkanV12);
    }
    if (vulkan_capability < 13) {
        output.features.reset(VulkanV13);
    }

    // Pick tight lower bound for HVX version. Use fall-through to clear redundant features
    int hvx_a = get_hvx_lower_bound(*this);
    int hvx_b = get_hvx_lower_bound(other);

    // Same trick as above for CUDA
    int hvx_version = std::min((unsigned)hvx_a, (unsigned)hvx_b);
    if (hvx_version < 62) {
        output.features.reset(HVX_v62);
    }
    if (hvx_version < 65) {
        output.features.reset(HVX_v65);
    }
    if (hvx_version < 66) {
        output.features.reset(HVX_v66);
    }

    result = output;
    return true;
}

namespace Internal {

void target_test() {
    Target t;
    for (const auto &feature : feature_name_map) {
        t.set_feature(feature.second);
    }
    for (int i = 0; i < (int)(Target::FeatureEnd); i++) {
        internal_assert(t.has_feature((Target::Feature)i)) << "Feature " << i << " not in feature_names_map.\n";
    }

    // 3 targets: {A,B,C}. Want gcd(A,B)=C
    std::vector<std::array<std::string, 3>> gcd_tests = {
        {{"x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma"}},
        {{"x86-64-linux-sse41-fma-no_asserts-no_runtime", "x86-64-linux-sse41-fma", "x86-64-linux-sse41-fma"}},
        {{"x86-64-linux-avx2-sse41", "x86-64-linux-sse41-fma", "x86-64-linux-sse41"}},
        {{"x86-64-linux-avx2-sse41", "x86-32-linux-sse41-fma", ""}},
        {{"x86-64-linux-cuda", "x86-64-linux", "x86-64-linux-cuda"}},
        {{"x86-64-linux-cuda-cuda_capability_50", "x86-64-linux-cuda", "x86-64-linux-cuda"}},
        {{"x86-64-linux-cuda-cuda_capability_50", "x86-64-linux-cuda-cuda_capability_30", "x86-64-linux-cuda-cuda_capability_30"}},
        {{"x86-64-linux-vulkan", "x86-64-linux", "x86-64-linux-vulkan"}},
        {{"x86-64-linux-vulkan-vk_v13", "x86-64-linux-vulkan", "x86-64-linux-vulkan"}},
        {{"x86-64-linux-vulkan-vk_v13", "x86-64-linux-vulkan-vk_v10", "x86-64-linux-vulkan-vk_v10"}},
        {{"hexagon-32-qurt-hvx_v65", "hexagon-32-qurt-hvx_v62", "hexagon-32-qurt-hvx_v62"}},
        {{"hexagon-32-qurt-hvx_v62", "hexagon-32-qurt", "hexagon-32-qurt"}},
        {{"hexagon-32-qurt-hvx_v62-hvx", "hexagon-32-qurt", ""}},
        {{"hexagon-32-qurt-hvx_v62-hvx", "hexagon-32-qurt-hvx", "hexagon-32-qurt-hvx"}},
    };

    for (const auto &test : gcd_tests) {
        Target result{};
        Target a{test[0]};
        Target b{test[1]};
        if (a.get_runtime_compatible_target(b, result)) {
            internal_assert(!test[2].empty() && result == Target{test[2]})
                << "Targets " << a.to_string() << " and " << b.to_string() << " were computed to have gcd "
                << result.to_string() << " but expected '" << test[2] << "'\n";
        } else {
            internal_assert(test[2].empty())
                << "Targets " << a.to_string() << " and " << b.to_string() << " were computed to have no gcd "
                << "but " << test[2] << " was expected.";
        }
    }

    internal_assert(Target().vector_bits == 0) << "Default Target vector_bits not 0.\n";
    internal_assert(Target("arm-64-linux-sve2-vector_bits_512").vector_bits == 512) << "Vector bits not parsed correctly.\n";
    Target with_vector_bits(Target::Linux, Target::ARM, 64, Target::ProcessorGeneric, {Target::SVE}, 512);
    internal_assert(with_vector_bits.vector_bits == 512) << "Vector bits not populated in constructor.\n";
    internal_assert(Target(with_vector_bits.to_string()).vector_bits == 512) << "Vector bits not round tripped properly.\n";

    std::cout << "Target test passed\n";
}

}  // namespace Internal

}  // namespace Halide

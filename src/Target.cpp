#include <array>
#include <iostream>
#include <string>

#include "Target.h"

#include "Debug.h"
#include "DeviceInterface.h"
#include "Error.h"
#include "Util.h"
#include "WasmExecutor.h"

#include "runtime/arm_cpu_detect.h"
#include "runtime/auxv_ops.h"
#include "runtime/powerpc_cpu_detect.h"
#include "runtime/x86_cpu_detect.h"

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
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <intrin.h>
#include <windows.h>
#endif  // _MSC_VER

#ifdef __APPLE__
#include <mach/machine.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

#if defined(__linux__) && (defined(__arm__) || defined(__aarch64__))
// The hwcap bit values live in runtime/arm_cpu_detect.h, shared with the
// runtime, rather than coming from <asm/hwcap.h>: the runtime can't include
// kernel headers, and when the two disagreed the compiler silently stopped
// detecting features whose macros the build machine's headers lacked. We still
// include the kernel header to cross-check the values we do use.
#include <asm/hwcap.h>
#include <sys/auxv.h>
#endif

/* Detect SME target attribute support */
#if defined(__aarch64__) && !defined(__arm__) &&                       \
    ((defined(__GNUC__) && !defined(__clang__) && (__GNUC__ >= 14)) || \
     (defined(__clang__) && (__clang_major__ >= 17)))
#define HAS_ATTR_TARGET_SME 1
#else
#define HAS_ATTR_TARGET_SME 0
#endif

namespace Halide {

using std::string;
using std::vector;

namespace {

namespace CpuDetect = Internal::CpuDetect;

// Common recording half of the shared host-CPU detection logic.
struct TargetFeatureSink {
    std::vector<Target::Feature> features;

    void set_feature(halide_target_feature_t feature) {
        features.push_back((Target::Feature)feature);
    }
};

#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_AMD64)

// The platform half of the shared detection logic in runtime/x86_cpu_detect.h.
// Unlike the runtime, we can emit cpuid/xgetbv directly.
struct X86HostOps : TargetFeatureSink {
    [[nodiscard]] CpuDetect::CpuidResult cpuid(uint32_t leaf, uint32_t subleaf) {
#if defined(_M_IX86) || defined(_M_AMD64)
        int info[4];
        __cpuidex(info, (int)leaf, (int)subleaf);
        return {(uint32_t)info[0], (uint32_t)info[1], (uint32_t)info[2], (uint32_t)info[3]};
#else
        // CPU feature detection code taken from ispc
        // (https://github.com/ispc/ispc/blob/master/builtins/dispatch.ll)
        CpuDetect::CpuidResult result;
        __asm__ __volatile__(
            "cpuid                 \n\t"
            : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
            : "0"(leaf), "2"(subleaf));
        return result;
#endif
    }

    [[nodiscard]] uint64_t xgetbv(uint32_t xcr) {
#if defined(_M_IX86) || defined(_M_AMD64)
        return _xgetbv(xcr);
#else
        uint32_t lo, hi;
        __asm__ __volatile__("xgetbv" : "=a"(lo), "=d"(hi) : "c"(xcr));
        return ((uint64_t)hi << 32) | lo;
#endif
    }

    void request_amx_os_permission() {
        CpuDetect::detail::request_linux_amx_permission();
    }
};

#endif  // defined(__x86_64__) || defined(__i386__) || defined(_MSC_VER)

#if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)

#if defined(__arm__)
constexpr CpuDetect::ArmArch host_arm_arch = CpuDetect::ArmArch::Arm32;
#else
constexpr CpuDetect::ArmArch host_arm_arch = CpuDetect::ArmArch::Arm64;
#endif

#ifdef __linux__
// The auxiliary vector is filled in by the kernel, so the values in the shared
// header have to match the ones the kernel headers define. Cross-check the ones
// this build's headers know about; older headers omit some, which is why we
// don't take the values from them in the first place.
#ifdef HWCAP_ASIMDHP
static_assert(CpuDetect::detail::hwcap_asimdhp(host_arm_arch) == HWCAP_ASIMDHP);
#endif
#ifdef AT_HWCAP
static_assert(CpuDetect::detail::at_hwcap == AT_HWCAP);
#endif
#ifdef AT_HWCAP2
static_assert(CpuDetect::detail::at_hwcap2 == AT_HWCAP2);
#endif
#ifdef HWCAP_ASIMDDP
static_assert(CpuDetect::detail::hwcap_asimddp(host_arm_arch) == HWCAP_ASIMDDP);
#endif
#if defined(HWCAP_SVE) && !defined(__arm__)
static_assert(CpuDetect::detail::hwcap_sve == HWCAP_SVE);
#endif
#if defined(HWCAP2_SVE2) && !defined(__arm__)
static_assert(CpuDetect::detail::hwcap2_sve2 == HWCAP2_SVE2);
#endif
#if defined(HWCAP2_SME2) && !defined(__arm__)
static_assert(CpuDetect::detail::hwcap2_sme2 == HWCAP2_SME2);
#endif
#endif  // __linux__

#ifdef __linux__
using ArmPlatformOps = CpuDetect::GetAuxValOps<TargetFeatureSink>;
#elif defined(__APPLE__)
using ArmPlatformOps = CpuDetect::SysctlByNameOps<TargetFeatureSink>;
#elif defined(_MSC_VER)
using ArmPlatformOps = CpuDetect::WindowsProcessorFeatureOps<TargetFeatureSink>;
#else
struct ArmPlatformOps : TargetFeatureSink {
    using ArmDetectionSource = CpuDetect::NullSource;
};
#endif

struct ArmHostOps : ArmPlatformOps {
#if defined(__aarch64__)
    __attribute__((target("+sve"))) int get_sve_vector_length() {
        register int result asm("w0");
        __asm__("cntb %x0, all, mul #8" : "=r"(result));
        return result;
    }

#if HAS_ATTR_TARGET_SME
    __attribute__((target("+sme"))) int get_sme_streaming_vector_length() {  // codespell:ignore sme
        register int result asm("w0");
        __asm__("rdsvl %x0, #8" : "=r"(result));
        return result;
    }
#else
    int get_sme_streaming_vector_length() {
        user_error << "Trying to get streaming_vector_length where SME is supposed to be unsupported\n";
        return 0;
    }
#endif
#endif
};

#endif  // ARM

#if defined(__powerpc__) && (defined(__FreeBSD__) || defined(__linux__))

// The values in the shared header have to match the ones the platform headers
// define, since the auxiliary vector is filled in by the OS. The runtime can't
// include those headers, so cross-check here, where we can.
static_assert(CpuDetect::detail::ppc_feature_has_altivec == PPC_FEATURE_HAS_ALTIVEC);
static_assert(CpuDetect::detail::ppc_feature_has_vsx == PPC_FEATURE_HAS_VSX);
static_assert(CpuDetect::detail::ppc_feature2_arch_2_07 == PPC_FEATURE2_ARCH_2_07);
#ifdef AT_HWCAP
static_assert(CpuDetect::detail::at_hwcap == AT_HWCAP);
#endif
#ifdef AT_HWCAP2
static_assert(CpuDetect::detail::at_hwcap2 == AT_HWCAP2);
#endif

#if defined(__linux__)
using PowerPCHostOps = CpuDetect::GetAuxValOps<TargetFeatureSink>;
#else
struct PowerPCHostOps : TargetFeatureSink {
    uint64_t get_hwcap() {
        uint64_t value = 0;
        elf_aux_info(AT_HWCAP, &value, sizeof(value));
        return value;
    }

    uint64_t get_hwcap2() {
        uint64_t value = 0;
        elf_aux_info(AT_HWCAP2, &value, sizeof(value));
        return value;
    }
};
#endif

#endif  // POWERPC

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
#if defined(__arm__) || defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)
    Target::Arch arch = Target::ARM;

    // The feature detection itself is shared with the runtime; see
    // runtime/arm_cpu_detect.h. We supply the platform half: asking the OS, and
    // recording what we're told. Which question to ask depends on the OS, not
    // the CPU, so there is one call per platform.
    ArmHostOps ops;
    CpuDetect::ArmDetection detection{false, false};

#ifdef __APPLE__
    detection = CpuDetect::detect_arm_features(ops, host_arm_arch);
#endif  // __APPLE__

#ifdef __linux__
    detection = CpuDetect::detect_arm_features(ops, host_arm_arch);
#endif

#ifdef _MSC_VER
    detection = CpuDetect::detect_arm_features(ops, host_arm_arch);
#endif

#if defined(__aarch64__)
    // SME2 is a hardware capability, but without HAS_ATTR_TARGET_SME the local
    // compiler can't emit the streaming-mode probe needed to determine the SME
    // streaming vector length, so don't advertise SME2 support in that case.
    if (!HAS_ATTR_TARGET_SME) {
        detection.have_sme2 = false;
    }
#endif

    for (Target::Feature f : ops.features) {
#if defined(__aarch64__)
        if (f == Target::SME2 && !detection.have_sme2) {
            continue;
        }
#endif
        initial_features.push_back(f);
    }

#if defined(__aarch64__)
    if (detection.have_scalable_vector) {
        vector_bits = ops.get_sve_vector_length();
    }

    if (detection.have_sme2) {
        const int streaming_vector_bits = ops.get_sme_streaming_vector_length();
        Target::Feature sme_svl = Target::sme_svl_feature_from_bits(streaming_vector_bits);
        user_assert(sme_svl != Target::FeatureEnd)
            << "Detected unsupported SME streaming vector length " << streaming_vector_bits << " bits.\n";
        initial_features.push_back(sme_svl);
    }
#else
    (void)detection;
#endif

#else
#if defined(__powerpc__) && (defined(__FreeBSD__) || defined(__linux__))
    Target::Arch arch = Target::POWERPC;

    // The feature detection itself is shared with the runtime; see
    // runtime/powerpc_cpu_detect.h. We supply the platform half: reading the
    // auxiliary vector and recording what it reports.
    PowerPCHostOps ops;
    const CpuDetect::PowerPCDetection detection =
        CpuDetect::detect_powerpc_features(ops);

    user_assert(detection.have_altivec)
        << "The POWERPC backend assumes at least AltiVec support. This machine does not appear to have AltiVec.\n";

    initial_features.insert(initial_features.end(), ops.features.begin(), ops.features.end());
#else
    Target::Arch arch = Target::X86;

    // The feature detection itself is shared with the runtime; see
    // runtime/x86_cpu_detect.h. We supply the platform half: issuing the
    // instructions, and recording what we're told.
    X86HostOps ops;
    const CpuDetect::X86Detection detection = CpuDetect::detect_x86_features(ops);

    user_assert(detection.have_sse2)
        << "The x86 backend assumes at least sse2 support. This machine does not appear to have sse2.\n"
        << "cpuid returned: "
        << std::hex << detection.leaf1.eax
        << ", " << detection.leaf1.ebx
        << ", " << detection.leaf1.ecx
        << ", " << detection.leaf1.edx
        << std::dec << "\n";

    initial_features.insert(initial_features.end(), ops.features.begin(), ops.features.end());
    processor = static_cast<Target::Processor>(detection.processor);
    vector_bits = detection.vector_bits;

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
            t.has_feature(Target::HVX_v68) ||
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
    if (t.has_feature(Target::HVX_v68)) {
        return 68;
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
    } else if (ver < 89) {
        return Target::CUDACapability86;
    } else if (ver < 90) {
        return Target::CUDACapability89;
    } else if (ver < 100) {
        return Target::CUDACapability90;
    } else if (ver < 120) {
        return Target::CUDACapability100;
    } else {
        return Target::CUDACapability120;
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

// Keep this list in sync in HalideGeneratorHelpers.cmake
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

// Keep this list in sync in HalideGeneratorHelpers.cmake
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
    {"tune_znver5", Target::Processor::ZnVer5},
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
    {"enable_backtraces", Target::EnableBacktraces},
    {"no_asserts", Target::NoAsserts},
    {"no_bounds_query", Target::NoBoundsQuery},
    {"sse41", Target::SSE41},
    {"avx", Target::AVX},
    {"avx2", Target::AVX2},
    {"avxvnni", Target::AVXVNNI},
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
    {"cuda_capability_89", Target::CUDACapability89},
    {"cuda_capability_90", Target::CUDACapability90},
    {"cuda_capability_100", Target::CUDACapability100},
    {"cuda_capability_120", Target::CUDACapability120},
    {"opencl", Target::OpenCL},
    {"cl_doubles", Target::CLDoubles},
    {"cl_half", Target::CLHalf},
    {"cl_atomics64", Target::CLAtomics64},
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
    {"hvx_v68", Target::HVX_v68},
    {"fuzz_float_stores", Target::FuzzFloatStores},
    {"soft_float_abi", Target::SoftFloatABI},
    {"msan", Target::MSAN},
    {"avx512", Target::AVX512},
    {"avx512_knl", Target::AVX512_KNL},
    {"avx512_skylake", Target::AVX512_Skylake},
    {"avx512_cannonlake", Target::AVX512_Cannonlake},
    {"avx512_sapphirerapids", Target::AVX512_SapphireRapids},
    {"avx512_zen4", Target::AVX512_Zen4},
    {"avx512_zen5", Target::AVX512_Zen5},
    {"trace_loads", Target::TraceLoads},
    {"trace_stores", Target::TraceStores},
    {"trace_realizations", Target::TraceRealizations},
    {"trace_pipeline", Target::TracePipeline},
    {"d3d12compute", Target::D3D12Compute},
    {"hlsl_sm60", Target::HLSL_SM60},
    {"hlsl_sm61", Target::HLSL_SM61},
    {"hlsl_sm62", Target::HLSL_SM62},
    {"hlsl_sm63", Target::HLSL_SM63},
    {"hlsl_sm64", Target::HLSL_SM64},
    {"hlsl_sm65", Target::HLSL_SM65},
    {"hlsl_sm66", Target::HLSL_SM66},
    {"hlsl_sm67", Target::HLSL_SM67},
    {"hlsl_sm68", Target::HLSL_SM68},
    {"hlsl_sm69", Target::HLSL_SM69},
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
    {"sme2", Target::SME2},
    {"sme_svl128", Target::SME_SVL128},
    {"sme_svl256", Target::SME_SVL256},
    {"sme_svl512", Target::SME_SVL512},
    {"sme_svl1024", Target::SME_SVL1024},
    {"sme_svl2048", Target::SME_SVL2048},
    {"arm_dot_prod", Target::ARMDotProd},
    {"arm_fp16", Target::ARMFp16},
    {"llvm_large_code_model", Target::LLVMLargeCodeModel},
    {"rvv", Target::RVV},
    {"armv8a", Target::ARMv8a},
    {"armv81a", Target::ARMv81a},
    {"armv82a", Target::ARMv82a},
    {"armv83a", Target::ARMv83a},
    {"armv84a", Target::ARMv84a},
    {"armv85a", Target::ARMv85a},
    {"armv86a", Target::ARMv86a},
    {"armv87a", Target::ARMv87a},
    {"armv88a", Target::ARMv88a},
    {"armv89a", Target::ARMv89a},
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
    {"avx10_1", Target::AVX10_1},
    {"x86apx", Target::X86APX},
    {"simulator", Target::Simulator},
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
        // debug(0) << first_dash << ", " << rest << "\n";
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
        !t.has_feature(Target::CUDACapability86) &&
        !t.has_feature(Target::CUDACapability89) &&
        !t.has_feature(Target::CUDACapability90) &&
        !t.has_feature(Target::CUDACapability100) &&
        !t.has_feature(Target::CUDACapability120)) {
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
                                SME2,
                                SME_SVL128,
                                SME_SVL256,
                                SME_SVL512,
                                SME_SVL1024,
                                SME_SVL2048,
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
                                AVXVNNI,
                                AVX512,
                                AVX512_Cannonlake,
                                AVX512_KNL,
                                AVX512_SapphireRapids,
                                AVX512_Skylake,
                                AVX512_Zen4,
                                AVX512_Zen5,
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
                                AVXVNNI,
                                AVX512,
                                AVX512_Cannonlake,
                                AVX512_KNL,
                                AVX512_SapphireRapids,
                                AVX512_Skylake,
                                AVX512_Zen4,
                                AVX512_Zen5,
                                F16C,
                                FMA,
                                FMA4,
                                HVX_128,
                                HVX_128,
                                HVX_v62,
                                HVX_v65,
                                HVX_v66,
                                HVX_v68,
                                NoNEON,
                                POWER_ARCH_2_07,
                                RVV,
                                SSE41,
                                SME_SVL128,
                                SME_SVL256,
                                SME_SVL512,
                                SME_SVL1024,
                                SME_SVL2048,
                                SME2,
                                SVE,
                                SVE2,
                                VSX,
                            });
    }

    // D3D12Compute SM version features require D3D12Compute to also be set.
    if (!has_feature(D3D12Compute)) {
        do_check_bad(*this, {
                                HLSL_SM60,
                                HLSL_SM61,
                                HLSL_SM62,
                                HLSL_SM63,
                                HLSL_SM64,
                                HLSL_SM65,
                                HLSL_SM66,
                                HLSL_SM67,
                                HLSL_SM68,
                                HLSL_SM69,
                            });
    }

    const int num_sme_svl_features =
        (int)has_feature(SME_SVL128) +
        (int)has_feature(SME_SVL256) +
        (int)has_feature(SME_SVL512) +
        (int)has_feature(SME_SVL1024) +
        (int)has_feature(SME_SVL2048);

    user_assert(num_sme_svl_features <= 1)
        << "Target may have at most one SME_SVL feature.\n";
    user_assert(!has_feature(SME2) || num_sme_svl_features == 1)
        << "Target feature sme2 requires exactly one SME_SVL feature.\n";
    user_assert(has_feature(SME2) || num_sme_svl_features == 0)
        << "Target features SME_SVL128, SME_SVL256, SME_SVL512, SME_SVL1024, and SME_SVL2048 require target feature sme2.\n";
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

Target::Feature Target::sme_svl_feature_from_bits(int bits) {
    switch (bits) {
    case 128:
        return Target::SME_SVL128;
    case 256:
        return Target::SME_SVL256;
    case 512:
        return Target::SME_SVL512;
    case 1024:
        return Target::SME_SVL1024;
    case 2048:
        return Target::SME_SVL2048;
    default:
        return Target::FeatureEnd;
    }
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
        result = Internal::replace_all(std::move(result), "trace_loads-trace_realizations-trace_stores", "trace_all");
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

namespace {

// The feature-implication table. Each entry {a, b} means "feature a implies
// feature b": there is no real device or configuration that has a set without
// b, so any target with a should be treated as also having b. The list is kept
// in topological order (an antecedent always appears before it is used as a
// consequent), so that a single forward pass sets every implied feature, and a
// single backward pass removes every redundant implied feature.
//
// Implications that depend on target state other than the feature set (the
// arch, os, or vector_bits) don't fit the simple pair model, and are handled
// directly in set_implied_features()/unset_implied_features().
const std::vector<std::pair<Target::Feature, Target::Feature>> &implied_feature_pairs() {
    static const std::vector<std::pair<Target::Feature, Target::Feature>> pairs = {
        // x86. Each AVX-family feature is a strict superset of the ones below
        // it, so it is impossible to have the higher one without the lower.
        {Target::AVX512_SapphireRapids, Target::AVX512_Zen4},
        {Target::AVX512_SapphireRapids, Target::AVXVNNI},
        {Target::AVX512_Zen5, Target::AVX512_Zen4},
        {Target::AVX512_Zen5, Target::AVXVNNI},
        {Target::AVX512_Zen4, Target::AVX512_Cannonlake},
        {Target::AVX512_Cannonlake, Target::AVX512_Skylake},
        {Target::AVX512_Skylake, Target::AVX512},
        {Target::AVX512_KNL, Target::AVX512},
        {Target::AVX512, Target::AVX2},
        // Every AVX2-enabled architecture also has F16C and FMA.
        {Target::AVX2, Target::F16C},
        {Target::AVX2, Target::FMA},
        {Target::AVX2, Target::AVX},
        {Target::AVX, Target::SSE41},

        // ARM
        {Target::SVE2, Target::ARMDotProd},
        {Target::SVE2, Target::ARMFp16},
        {Target::SVE, Target::ARMFp16},
        {Target::SME2, Target::ARMDotProd},
        {Target::SME2, Target::ARMFp16},
        // ARMFp16 implies ARM v8.2-A; we don't know of any device where that
        // doesn't hold. The v8.x cascade below then fills in v8.1a and v8a.
        {Target::ARMFp16, Target::ARMv82a},
        // The ARM v8.x version features form a descending chain: each level
        // implies the one below it, down to v8a.
        {Target::ARMv89a, Target::ARMv88a},
        {Target::ARMv88a, Target::ARMv87a},
        {Target::ARMv87a, Target::ARMv86a},
        {Target::ARMv86a, Target::ARMv85a},
        {Target::ARMv85a, Target::ARMv84a},
        {Target::ARMv84a, Target::ARMv83a},
        {Target::ARMv83a, Target::ARMv82a},
        {Target::ARMv82a, Target::ARMv81a},
        {Target::ARMv81a, Target::ARMv8a},

        // Tracing loads or stores also produces the enclosing realization
        // begin/end events, so that the traced loads and stores have context.
        {Target::TraceLoads, Target::TraceRealizations},
        {Target::TraceStores, Target::TraceRealizations},
    };
    return pairs;
}

}  // namespace

void Target::set_implied_features() {
    // Implications that depend on more than just the feature set.
    if (arch == X86 && has_feature(AVX10_1)) {
        // AVX10.1 at a given vector width supports the corresponding legacy
        // AVX feature set. The pairs below then cascade further.
        if (vector_bits >= 256) {
            set_feature(AVX2);
        }
        if (vector_bits >= 512) {
            set_feature(AVX512_SapphireRapids);
        }
    }
    if (arch == ARM && os == OSX) {
        // Apple silicon implements at least the ARM v8.4-A spec.
        set_feature(ARMv84a);
    }

    // Simple feature -> feature implications. One forward pass suffices because
    // the table is topologically sorted.
    for (const auto &[feature, implied] : implied_feature_pairs()) {
        if (has_feature(feature)) {
            set_feature(implied);
        }
    }
}

void Target::unset_implied_features() {
    // Walk the table backwards, clearing any feature that is implied by another
    // feature that remains set. Because the table is topologically sorted,
    // walking backwards guarantees a consequent is only cleared after it has
    // been used as an antecedent, so a chain collapses to just its highest
    // feature in one pass.
    const auto &pairs = implied_feature_pairs();
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it) {
        if (has_feature(it->first)) {
            set_feature(it->second, false);
        }
    }

    // Undo the conditional implications from set_implied_features(). These run
    // after the pair loop, mirroring how their seeds run before it there.
    if (arch == X86 && has_feature(AVX10_1)) {
        if (vector_bits >= 512) {
            set_feature(AVX512_SapphireRapids, false);
        }
        if (vector_bits >= 256) {
            set_feature(AVX2, false);
        }
    }
    if (arch == ARM && os == OSX) {
        set_feature(ARMv84a, false);
    }
}

Target Target::with_implied_features() const {
    Target copy = *this;
    copy.set_implied_features();
    return copy;
}

Target Target::without_implied_features() const {
    Target copy = *this;
    copy.unset_implied_features();
    return copy;
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
    if (has_feature(Target::CUDACapability89)) {
        return 89;
    }
    if (has_feature(Target::CUDACapability90)) {
        return 90;
    }
    if (has_feature(Target::CUDACapability100)) {
        return 100;
    }
    if (has_feature(Target::CUDACapability120)) {
        return 120;
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

int Target::get_d3d12compute_capability_lower_bound() const {
    if (!has_feature(Target::D3D12Compute)) {
        return -1;
    }
    if (has_feature(Target::HLSL_SM60)) {
        return 60;
    }
    if (has_feature(Target::HLSL_SM61)) {
        return 61;
    }
    if (has_feature(Target::HLSL_SM62)) {
        return 62;
    }
    if (has_feature(Target::HLSL_SM63)) {
        return 63;
    }
    if (has_feature(Target::HLSL_SM64)) {
        return 64;
    }
    if (has_feature(Target::HLSL_SM65)) {
        return 65;
    }
    if (has_feature(Target::HLSL_SM66)) {
        return 66;
    }
    if (has_feature(Target::HLSL_SM67)) {
        return 67;
    }
    if (has_feature(Target::HLSL_SM68)) {
        return 68;
    }
    if (has_feature(Target::HLSL_SM69)) {
        return 69;
    }
    return 51;  // default: SM 5.1 (FXC)
}

int Target::get_arm_v8_lower_bound() const {
    if (has_feature(Target::ARMv8a)) {
        return 80;
    }
    if (has_feature(Target::ARMv81a)) {
        return 81;
    }
    if (has_feature(Target::ARMv82a)) {
        return 82;
    }
    if (has_feature(Target::ARMv83a)) {
        return 83;
    }
    if (has_feature(Target::ARMv84a)) {
        return 84;
    }
    if (has_feature(Target::ARMv85a)) {
        return 85;
    }
    if (has_feature(Target::ARMv86a)) {
        return 86;
    }
    if (has_feature(Target::ARMv87a)) {
        return 87;
    }
    if (has_feature(Target::ARMv88a)) {
        return 88;
    }
    if (has_feature(Target::ARMv89a)) {
        return 89;
    }
    return -1;
}

bool Target::supports_type(const Type &t) const {
    if (t.bits() == 64) {
        if (t.is_float()) {
            return (!has_feature(Metal) &&
                    (!has_feature(D3D12Compute) || get_d3d12compute_capability_lower_bound() >= 60) &&
                    (!has_feature(Target::OpenCL) || has_feature(Target::CLDoubles)) &&
                    (!has_feature(Vulkan) || has_feature(Target::VulkanFloat64)) &&
                    !has_feature(WebGPU));
        } else {
            return (!has_feature(Metal) &&
                    (!has_feature(D3D12Compute) || get_d3d12compute_capability_lower_bound() >= 60) &&
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
        // SM 5.1 (FXC): no 64-bit types. float16 and int8 work via widening.
        // SM 6.0+: 64-bit int and float (double, int64_t, uint64_t) supported.
        // SM 6.2+: native 16-bit float (float16_t) and int (int16_t, uint16_t).
        // SM 6.6+: native 8-bit int (int8_t, uint8_t). Earlier SMs widen to int32.
        // SM 6.9+: long vectors (5–1024 lanes) via vector<T, N> syntax.
        if (t.bits() == 64) {
            return get_d3d12compute_capability_lower_bound() >= 60;
        }
        if (t.lanes() > 4) {
            return get_d3d12compute_capability_lower_bound() >= 69;
        }
        return true;
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
    case DeviceAPI::SMEStreaming:
        return Target::SME2;
    default:
        return Target::FeatureEnd;
    }
}

int Target::sme_streaming_vector_bits() const {
    int result = 0;
    auto set_result = [&result](int bits) {
        user_assert(result == 0)
            << "Target may have at most one SME_SVL feature.\n";
        result = bits;
    };
    if (has_feature(Target::SME_SVL128)) {
        set_result(128);
    }
    if (has_feature(Target::SME_SVL256)) {
        set_result(256);
    }
    if (has_feature(Target::SME_SVL512)) {
        set_result(512);
    }
    if (has_feature(Target::SME_SVL1024)) {
        set_result(1024);
    }
    if (has_feature(Target::SME_SVL2048)) {
        set_result(2048);
    }
    return result;
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
                           has_feature(Halide::Target::AVX512_Cannonlake) ||
                           has_feature(Halide::Target::AVX512_Zen4) ||
                           has_feature(Halide::Target::AVX512_Zen5) ||
                           has_feature(Halide::Target::AVX512_SapphireRapids))) {
            // AVX512BW exists on any of these avx512 variants
            return 64 / data_size;
        } else if (t.is_float() && (has_feature(Halide::Target::AVX512) ||
                                    has_feature(Halide::Target::AVX512_KNL) ||
                                    has_feature(Halide::Target::AVX512_Skylake) ||
                                    has_feature(Halide::Target::AVX512_Cannonlake) ||
                                    has_feature(Halide::Target::AVX512_Zen4) ||
                                    has_feature(Halide::Target::AVX512_Zen5) ||
                                    has_feature(Halide::Target::AVX512_SapphireRapids))) {
            // AVX512F is on all AVX512 architectures
            return 64 / data_size;
        } else if (has_feature(Halide::Target::AVX2) ||
                   has_feature(Halide::Target::AVX512) ||
                   has_feature(Halide::Target::AVX512_KNL)) {
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

    const std::vector<Feature> union_features = {{
        // These are true union features.
        CUDA,
        D3D12Compute,
        Metal,
        NoNEON,
        OpenCL,
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
        CUDACapability89,
        CUDACapability90,
        CUDACapability100,
        CUDACapability120,

        HVX_v62,
        HVX_v65,
        HVX_v66,
        HVX_v68,

        VulkanV10,
        VulkanV12,
        VulkanV13,

        HLSL_SM60,
        HLSL_SM61,
        HLSL_SM62,
        HLSL_SM63,
        HLSL_SM64,
        HLSL_SM65,
        HLSL_SM66,
        HLSL_SM67,
        HLSL_SM68,
        HLSL_SM69,

        ARMv8a,
        ARMv81a,
        ARMv82a,
        ARMv83a,
        ARMv84a,
        ARMv85a,
        ARMv86a,
        ARMv87a,
        ARMv88a,
        ARMv89a,
    }};

    const std::vector<Feature> intersection_features = {{
        ARMv7s,
        AVX,
        AVX2,
        AVXVNNI,
        AVX512,
        AVX512_Cannonlake,
        AVX512_KNL,
        AVX512_SapphireRapids,
        AVX512_Skylake,
        AVX512_Zen4,
        AVX512_Zen5,
        F16C,
        FMA,
        FMA4,
        SSE41,
        VSX,
    }};

    const std::vector<Feature> matching_features = {{
        ASAN,
        Debug,
        EnableBacktraces,
        HexagonDma,
        HVX,
        MSAN,
        SoftFloatABI,
        TSAN,
        WasmThreads,
        SanitizerCoverage,
        Simulator,
    }};

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
        debug(1) << "runtime targets must agree on platform (arch-bits-os)\n"
                 << "  this:  " << *this << "\n"
                 << "  other: " << other << "\n";
        return false;
    }

    if ((features & matching_mask) != (other.features & matching_mask)) {
        debug(1) << "runtime targets must agree on SoftFloatABI, Debug, TSAN, ASAN, MSAN, HVX, HexagonDma, SanitizerCoverage\n"
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
    if (cuda_capability < 89) {
        output.features.reset(CUDACapability89);
    }
    if (cuda_capability < 90) {
        output.features.reset(CUDACapability90);
    }
    if (cuda_capability < 100) {
        output.features.reset(CUDACapability100);
    }
    if (cuda_capability < 120) {
        output.features.reset(CUDACapability120);
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

    // Pick tight lower bound for D3D12Compute SM version. Use fall-through to clear redundant features
    int d3d12_sm_a = get_d3d12compute_capability_lower_bound();
    int d3d12_sm_b = other.get_d3d12compute_capability_lower_bound();

    // Same trick as CUDA: -1 (unused) becomes large when cast to unsigned, so min gives the true lower bound.
    int d3d12_sm = std::min((unsigned)d3d12_sm_a, (unsigned)d3d12_sm_b);
    if (d3d12_sm < 60) {
        output.features.reset(HLSL_SM60);
    }
    if (d3d12_sm < 61) {
        output.features.reset(HLSL_SM61);
    }
    if (d3d12_sm < 62) {
        output.features.reset(HLSL_SM62);
    }
    if (d3d12_sm < 63) {
        output.features.reset(HLSL_SM63);
    }
    if (d3d12_sm < 64) {
        output.features.reset(HLSL_SM64);
    }
    if (d3d12_sm < 65) {
        output.features.reset(HLSL_SM65);
    }
    if (d3d12_sm < 66) {
        output.features.reset(HLSL_SM66);
    }
    if (d3d12_sm < 67) {
        output.features.reset(HLSL_SM67);
    }
    if (d3d12_sm < 68) {
        output.features.reset(HLSL_SM68);
    }
    if (d3d12_sm < 69) {
        output.features.reset(HLSL_SM69);
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
    if (hvx_version < 68) {
        output.features.reset(HVX_v68);
    }

    // Pick tight lower bound for ARM capability. Use fall-through to clear redundant features
    int arm_v8_a = get_arm_v8_lower_bound();
    int arm_v8_b = other.get_arm_v8_lower_bound();

    // Same trick as above for CUDA
    int arm_v8_capability = (int)std::min((unsigned)arm_v8_a, (unsigned)arm_v8_b);
    if (arm_v8_capability < 80) {
        output.features.reset(ARMv8a);
    }
    if (arm_v8_capability < 81) {
        output.features.reset(ARMv81a);
    }
    if (arm_v8_capability < 82) {
        output.features.reset(ARMv82a);
    }
    if (arm_v8_capability < 83) {
        output.features.reset(ARMv83a);
    }
    if (arm_v8_capability < 84) {
        output.features.reset(ARMv84a);
    }
    if (arm_v8_capability < 85) {
        output.features.reset(ARMv85a);
    }
    if (arm_v8_capability < 86) {
        output.features.reset(ARMv86a);
    }
    if (arm_v8_capability < 87) {
        output.features.reset(ARMv87a);
    }
    if (arm_v8_capability < 88) {
        output.features.reset(ARMv88a);
    }
    if (arm_v8_capability < 89) {
        output.features.reset(ARMv89a);
    }

    result = output;
    return true;
}

}  // namespace Halide

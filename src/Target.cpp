#include <iostream>
#include <string>

#include "Target.h"

#include "Debug.h"
#include "DeviceInterface.h"
#include "Error.h"
#include "LLVM_Headers.h"
#include "Util.h"

#if defined(__powerpc__) && defined(__linux__)
// This uses elf.h and must be included after "LLVM_Headers.h", which
// uses llvm/support/Elf.h.
#include <sys/auxv.h>
#endif

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

#ifdef _LP64
static void cpuid(int info[4], int infoType, int extra) {
    __asm__ __volatile__ (
        "cpuid                 \n\t"
        : "=a" (info[0]), "=b" (info[1]), "=c" (info[2]), "=d" (info[3])
        : "0" (infoType), "2" (extra));
}
#else
static void cpuid(int info[4], int infoType, int extra) {
    // We save %ebx in case it's the PIC register
    __asm__ __volatile__ (
        "mov{l}\t{%%}ebx, %1  \n\t"
        "cpuid                 \n\t"
        "xchg{l}\t{%%}ebx, %1  \n\t"
        : "=a" (info[0]), "=r" (info[1]), "=c" (info[2]), "=d" (info[3])
        : "0" (infoType), "2" (extra));
}
#endif
#endif
#endif

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
    std::vector<Target::Feature> initial_features;

#if __mips__ || __mips || __MIPS__
    Target::Arch arch = Target::MIPS;
#else
#if defined(__arm__) || defined(__aarch64__)
    Target::Arch arch = Target::ARM;
#else
#if defined(__powerpc__) && defined(__linux__)
    Target::Arch arch = Target::POWERPC;

    unsigned long hwcap = getauxval(AT_HWCAP);
    unsigned long hwcap2 = getauxval(AT_HWCAP2);
    bool have_altivec = (hwcap & PPC_FEATURE_HAS_ALTIVEC) != 0;
    bool have_vsx     = (hwcap & PPC_FEATURE_HAS_VSX) != 0;
    bool arch_2_07    = (hwcap2 & PPC_FEATURE2_ARCH_2_07) != 0;

    user_assert(have_altivec)
        << "The POWERPC backend assumes at least AltiVec support. This machine does not appear to have AltiVec.\n";

    std::vector<Target::Feature> initial_features;
    if (have_vsx)     initial_features.push_back(Target::VSX);
    if (arch_2_07)    initial_features.push_back(Target::POWER_ARCH_2_07);
#else
    Target::Arch arch = Target::X86;

    int info[4];
    cpuid(info, 1, 0);
    bool have_sse41  = (info[2] & (1 << 19)) != 0;
    bool have_sse2   = (info[3] & (1 << 26)) != 0;
    bool have_avx    = (info[2] & (1 << 28)) != 0;
    bool have_f16c   = (info[2] & (1 << 29)) != 0;
    bool have_rdrand = (info[2] & (1 << 30)) != 0;
    bool have_fma    = (info[2] & (1 << 12)) != 0;

    user_assert(have_sse2)
        << "The x86 backend assumes at least sse2 support. This machine does not appear to have sse2.\n"
        << "cpuid returned: "
        << std::hex << info[0]
        << ", " << info[1]
        << ", " << info[2]
        << ", " << info[3]
        << std::dec << "\n";

    if (have_sse41) initial_features.push_back(Target::SSE41);
    if (have_avx)   initial_features.push_back(Target::AVX);
    if (have_f16c)  initial_features.push_back(Target::F16C);
    if (have_fma)   initial_features.push_back(Target::FMA);

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
        const uint32_t avx512_cannonlake = avx512_skylake | avx512ifma; // Assume ifma => vbmi
        if ((info2[1] & avx2) == avx2) {
            initial_features.push_back(Target::AVX2);
        }
        if ((info2[1] & avx512) == avx512) {
            initial_features.push_back(Target::AVX512);
            if ((info2[1] & avx512_knl) == avx512_knl) {
                initial_features.push_back(Target::AVX512_KNL);
            }
            if ((info2[1] & avx512_skylake) == avx512_skylake) {
                initial_features.push_back(Target::AVX512_Skylake);
            }
            if ((info2[1] & avx512_cannonlake) == avx512_cannonlake) {
                initial_features.push_back(Target::AVX512_Cannonlake);
            }
        }
    }
#ifdef _WIN32
#ifndef _MSC_VER
    initial_features.push_back(Target::MinGW);
#endif
#endif

#endif
#endif
#endif

    return Target(os, arch, bits, initial_features);
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

const std::map<std::string, Target::OS> os_name_map = {
    {"os_unknown", Target::OSUnknown},
    {"linux", Target::Linux},
    {"windows", Target::Windows},
    {"osx", Target::OSX},
    {"android", Target::Android},
    {"ios", Target::IOS},
    {"qurt", Target::QuRT},
    {"noos", Target::NoOS},
};

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
    {"mips", Target::MIPS},
    {"powerpc", Target::POWERPC},
    {"hexagon", Target::Hexagon},
};

bool lookup_arch(const std::string &tok, Target::Arch &result) {
    auto arch_iter = arch_name_map.find(tok);
    if (arch_iter != arch_name_map.end()) {
        result = arch_iter->second;
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
    {"opencl", Target::OpenCL},
    {"cl_doubles", Target::CLDoubles},
    {"cl_half", Target::CLHalf},
    {"opengl", Target::OpenGL},
    {"openglcompute", Target::OpenGLCompute},
    {"user_context", Target::UserContext},
    {"matlab", Target::Matlab},
    {"profile", Target::Profile},
    {"no_runtime", Target::NoRuntime},
    {"metal", Target::Metal},
    {"mingw", Target::MinGW},
    {"c_plus_plus_name_mangling", Target::CPlusPlusMangling},
    {"large_buffers", Target::LargeBuffers},
    {"hexagon_dma", Target::HexagonDma},
    {"hvx_64", Target::HVX_64},
    {"hvx_128", Target::HVX_128},
    {"hvx_v62", Target::HVX_v62},
    {"hvx_v65", Target::HVX_v65},
    {"hvx_v66", Target::HVX_v66},
    {"hvx_shared_object", Target::HVX_shared_object},
    {"fuzz_float_stores", Target::FuzzFloatStores},
    {"soft_float_abi", Target::SoftFloatABI},
    {"msan", Target::MSAN},
    {"avx512", Target::AVX512},
    {"avx512_knl", Target::AVX512_KNL},
    {"avx512_skylake", Target::AVX512_Skylake},
    {"avx512_cannonlake", Target::AVX512_Cannonlake},
    {"trace_loads", Target::TraceLoads},
    {"trace_stores", Target::TraceStores},
    {"trace_realizations", Target::TraceRealizations},
};

bool lookup_feature(const std::string &tok, Target::Feature &result) {
    auto feature_iter = feature_name_map.find(tok);
    if (feature_iter != feature_name_map.end()) {
        result = feature_iter->second;
        return true;
    }
    return false;
}

} // End anonymous namespace

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
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
    host.set_feature(Target::MSAN);
#endif
#endif
    string target = Internal::get_env_variable("HL_JIT_TARGET");
    if (target.empty()) {
        return host;
    } else {
        Target t(target);
        t.set_feature(Target::JIT);
        user_assert(t.os == host.os && t.arch == host.arch && t.bits == host.bits)
            << "HL_JIT_TARGET must match the host OS, architecture, and bit width.\n"
            << "HL_JIT_TARGET was " << target << ". "
            << "Host is " << host.to_string() << ".\n";
        return t;
    }
}

namespace {
bool merge_string(Target &t, const std::string &target) {
    string rest = target;
    vector<string> tokens;
    size_t first_dash;
    while ((first_dash = rest.find('-')) != string::npos) {
        //Internal::debug(0) << first_dash << ", " << rest << "\n";
        tokens.push_back(rest.substr(0, first_dash));
        rest = rest.substr(first_dash + 1);
    }
    tokens.push_back(rest);

    bool os_specified = false, arch_specified = false, bits_specified = false, features_specified = false;

    for (size_t i = 0; i < tokens.size(); i++) {
        const string &tok = tokens[i];
        Target::Feature feature;

        if (tok == "host") {
            if (i > 0) {
                // "host" is now only allowed as the first token.
                return false;
            }
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
        } else if (lookup_feature(tok, feature)) {
            t.set_feature(feature);
            features_specified = true;
        } else {
            return false;
        }
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
    for (auto os_entry : os_name_map) {
        oses += separator + os_entry.first;
        separator = ", ";
    }
    separator = "";
    // Format the features to go one feature over 70 characters per line,
    // assume the first line starts with "Features are ".
    int line_char_start = -(int)sizeof("Features are");
    std::string features;
    for (auto feature_entry : feature_name_map) {
        features += separator + feature_entry.first;
        if (features.length() - line_char_start > 70) {
            separator = "\n";
            line_char_start = features.length();
        } else {
            separator = ", ";
        }
    }
    user_error << "Did not understand Halide target " << target << "\n"
               << "Expected format is arch-bits-os-feature1-feature2-...\n"
               << "Where arch is: " << architectures << ".\n"
               << "bits is either 32 or 64.\n"
               << "os is: " << oses << ".\n"
               << "\n"
               << "If arch, bits, or os are omitted, they default to the host.\n"
               << "\n"
               << "Features are: " << features << ".\n"
               << "\n"
               << "The target can also begin with \"host\", which sets the "
               << "host's architecture, os, and feature set, with the "
               << "exception of the GPU runtimes, which default to off.\n"
               << "\n"
               << "On this platform, the host target is: " << get_host_target().to_string() << "\n";
}

}

Target::Target(const std::string &target) {
    Target host = get_host_target();

    if (target.empty()) {
        // If nothing is specified, use the full host target.
        *this = host;
    } else {

        // Default to the host OS and architecture in case of partially
        // specified targets (e.g. x86-64-cuda doesn't specify the OS, so
        // use the host OS).
        os = host.os;
        arch = host.arch;
        bits = host.bits;

        if (!merge_string(*this, target)) {
            bad_target_string(target);
        }
    }
}

Target::Target(const char *s) {
    *this = Target(std::string(s));
}

bool Target::validate_target_string(const std::string &s) {
    Target t;
    return merge_string(t, s);
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
    for (const auto &feature_entry : feature_name_map) {
        if (has_feature(feature_entry.second)) {
            result += "-" + feature_entry.first;
        }
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
#if !defined(WITH_MIPS)
    bad |= arch == Target::MIPS;
#endif
#if !defined(WITH_POWERPC)
    bad |= arch == Target::POWERPC;
#endif
#if !defined(WITH_HEXAGON)
    bad |= arch == Target::Hexagon;
#endif
#if !defined(WITH_PTX)
    bad |= has_feature(Target::CUDA);
#endif
#if !defined(WITH_OPENCL)
    bad |= has_feature(Target::OpenCL);
#endif
#if !defined(WITH_METAL)
    bad |= has_feature(Target::Metal);
#endif
#if !defined(WITH_OPENGL)
    bad |= has_feature(Target::OpenGL) || has_feature(Target::OpenGLCompute);
#endif
    return !bad;
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
    }

    return true;
}

bool Target::supports_device_api(DeviceAPI api) const {
    switch (api) {
    case DeviceAPI::None:        return true;
    case DeviceAPI::Host:        return true;
    case DeviceAPI::Default_GPU: return has_gpu_feature() || has_feature(Target::OpenGLCompute);
    case DeviceAPI::Hexagon:     return has_feature(Target::HVX_64) || has_feature(Target::HVX_128);
    case DeviceAPI::HexagonDma:  return has_feature(Target::HexagonDma);
    default:                     return has_feature(target_feature_for_device_api(api));
    }
}

Target::Feature target_feature_for_device_api(DeviceAPI api) {
    switch (api) {
    case DeviceAPI::CUDA:          return Target::CUDA;
    case DeviceAPI::OpenCL:        return Target::OpenCL;
    case DeviceAPI::GLSL:          return Target::OpenGL;
    case DeviceAPI::OpenGLCompute: return Target::OpenGLCompute;
    case DeviceAPI::Metal:         return Target::Metal;
    case DeviceAPI::Hexagon:       return Target::HVX_128;
    default:                       return Target::FeatureEnd;
    }
}

namespace Internal {

EXPORT void target_test() {
    Target t;
    for (const auto &feature : feature_name_map) {
        t.set_feature(feature.second);
    }
    for (int i = 0; i < (int)(Target::FeatureEnd); i++) {
        if (i == halide_target_feature_unused_23) continue;
        internal_assert(t.has_feature((Target::Feature)i)) << "Feature " << i << " not in feature_names_map.\n";
    }
    std::cout << "Target test passed" << std::endl;
}


}

}

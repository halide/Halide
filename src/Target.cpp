#include <iostream>
#include <string>

#include "Target.h"
#include "Debug.h"
#include "Error.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {

using std::string;
using std::vector;

namespace {
#ifndef __arm__

#ifdef _MSC_VER
static void cpuid(int info[4], int infoType, int extra) {
    __cpuidex(info, infoType, extra);
}

#else
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
}

Target get_host_target() {
    Target::OS os = Target::OSUnknown;
    #ifdef __linux__
    os = Target::Linux;
    #endif
    #ifdef _MSC_VER
    os = Target::Windows;
    #endif
    #ifdef __APPLE__
    os = Target::OSX;
    #endif

    bool use_64_bits = (sizeof(size_t) == 8);
    int bits = use_64_bits ? 64 : 32;

    #if __mips__ || __mips || __MIPS__
    Target::Arch arch = Target::MIPS;
    return Target(os, arch, bits);
    #else
    #ifdef __arm__
    Target::Arch arch = Target::ARM;
    return Target(os, arch, bits);
    #else

    Target::Arch arch = Target::X86;

    int info[4];
    cpuid(info, 1, 0);
    bool have_sse41 = info[2] & (1 << 19);
    bool have_sse2 = info[3] & (1 << 26);
    bool have_avx = info[2] & (1 << 28);
    bool have_f16c = info[2] & (1 << 29);
    bool have_rdrand = info[2] & (1 << 30);
    bool have_fma = info[2] & (1 << 12);

    user_assert(have_sse2)
        << "The x86 backend assumes at least sse2 support. This machine does not appear to have sse2.\n"
        << "cpuid returned: "
        << std::hex << info[0]
        << ", " << info[1]
        << ", " << info[2]
        << ", " << info[3]
        << std::dec << "\n";

    std::vector<Target::Feature> initial_features;
    if (have_sse41) initial_features.push_back(Target::SSE41);
    if (have_avx)   initial_features.push_back(Target::AVX);
    if (have_f16c)  initial_features.push_back(Target::F16C);
    if (have_fma)   initial_features.push_back(Target::FMA);

    if (use_64_bits && have_avx && have_f16c && have_rdrand) {
        // So far, so good.  AVX2?
        // Call cpuid with eax=7, ecx=0
        int info2[4];
        cpuid(info2, 7, 0);
        bool have_avx2 = info[1] & (1 << 5);
        if (have_avx2) {
            initial_features.push_back(Target::AVX2);
        }
    }

    return Target(os, arch, bits, initial_features);
#endif
#endif
}

namespace {
string get_env(const char *name) {
#ifdef _WIN32
    char buf[128];
    size_t read = 0;
    getenv_s(&read, buf, name);
    if (read) {
        return string(buf);
    } else {
        return "";
    }
#else
    char *buf = getenv(name);
    if (buf) {
        return string(buf);
    } else {
        return "";
    }
#endif
}
}

Target get_target_from_environment() {
    string target = get_env("HL_TARGET");
    if (target.empty()) {
        return get_host_target();
    } else {
        return parse_target_string(target);
    }
}

Target get_jit_target_from_environment() {
    Target host = get_host_target();
    host.set_feature(Target::JIT);
    string target = get_env("HL_JIT_TARGET");
    if (target.empty()) {
        return host;
    } else {
        Target t = parse_target_string(target);
        t.set_feature(Target::JIT);
        user_assert(t.os == host.os && t.arch == host.arch && t.bits == host.bits)
            << "HL_JIT_TARGET must match the host OS, architecture, and bit width.\n"
            << "HL_JIT_TARGET was " << target << ". "
            << "Host is " << host.to_string() << ".\n";
        return t;
    }
}

Target parse_target_string(const std::string &target) {
    Target host = get_host_target();

    if (target.empty()) {
        // If nothing is specified, use the host target.
        return host;
    }

    // Default to the host OS and architecture.
    Target t;
    t.os = host.os;
    t.arch = host.arch;
    t.bits = host.bits;

    if (!t.merge_string(target)) {
        user_error << "Did not understand HL_TARGET=" << target << "\n"
                   << "Expected format is arch-os-feature1-feature2-... "
                   << "Where arch is x86-32, x86-64, arm-32, arm-64, pnacl, mips"
                   << "and os is linux, windows, osx, nacl, ios, or android. "
                   << "If arch or os are omitted, they default to the host. "
                   << "Features include sse41, avx, avx2, armv7s, cuda, "
                   << "opencl, no_asserts, no_bounds_query, and debug.\n"
                   << "HL_TARGET can also begin with \"host\", which sets the "
                   << "host's architecture, os, and feature set, with the "
                   << "exception of the GPU runtimes, which default to off.\n"
                   << "On this platform, the host target is: " << host.to_string() << "\n";
    }

    return t;
}

bool Target::merge_string(const std::string &target) {
    string rest = target;
    vector<string> tokens;
    size_t first_dash;
    while ((first_dash = rest.find('-')) != string::npos) {
        //Internal::debug(0) << first_dash << ", " << rest << "\n";
        tokens.push_back(rest.substr(0, first_dash));
        rest = rest.substr(first_dash + 1);
    }
    tokens.push_back(rest);

    bool os_specified = false, arch_specified = false, bits_specified = false;

    for (size_t i = 0; i < tokens.size(); i++) {
        bool is_arch = false, is_os = false, is_bits = false;
        const string &tok = tokens[i];
        if (tok == "x86") {
            arch = Target::X86;
            is_arch = true;
        } else if (tok == "arm") {
            arch = Target::ARM;
            is_arch = true;
        } else if (tok == "pnacl") {
            arch = Target::PNaCl;
            is_arch = true;
        } else if (tok == "mips") {
            arch = Target::MIPS;
            is_arch = true;
        } else if (tok == "32") {
            bits = 32;
            is_bits = true;
        } else if (tok == "64") {
            bits = 64;
            is_bits = true;
        } else if (tok == "linux") {
            os = Target::Linux;
            is_os = true;
        } else if (tok == "windows") {
            os = Target::Windows;
            is_os = true;
        } else if (tok == "nacl") {
            os = Target::NaCl;
            is_os = true;
        } else if (tok == "osx") {
            os = Target::OSX;
            is_os = true;
        } else if (tok == "android") {
            os = Target::Android;
            is_os = true;
        } else if (tok == "ios") {
            os = Target::IOS;
            is_os = true;
        } else if (tok == "host") {
            if (i > 0) {
                // "host" is now only allowed as the first token.
                return false;
            }
            *this = get_host_target();
            is_os = true;
            is_arch = true;
            is_bits = true;
        } else if (tok == "jit") {
            set_feature(Target::JIT);
        } else if (tok == "sse41") {
            set_feature(Target::SSE41);
        } else if (tok == "avx") {
            set_features({Target::SSE41, Target::AVX});
        } else if (tok == "avx2") {
            set_features({Target::SSE41, Target::AVX, Target::AVX2});
        } else if (tok == "armv7s") {
            set_feature(Target::ARMv7s);
        } else if (tok == "no_neon") {
            set_feature(Target::NoNEON);
        } else if (tok == "cuda") {
            set_feature(Target::CUDA);
        } else if (tok == "ptx") {
            user_error << "The 'ptx' target feature flag is deprecated, use 'cuda' instead\n";
        } else if (tok == "cuda_capability_30") {
            set_features({Target::CUDA, Target::CUDACapability30});
        } else if (tok == "cuda_capability_32") {
            set_features({Target::CUDA, Target::CUDACapability32});
        } else if (tok == "cuda_capability_35") {
            set_features({Target::CUDA, Target::CUDACapability35});
        } else if (tok == "cuda_capability_50") {
            set_features({Target::CUDA, Target::CUDACapability50});
        } else if (tok == "opencl") {
            set_feature(Target::OpenCL);
        } else if (tok == "debug" || tok == "gpu_debug") {
            set_feature(Target::Debug);
        } else if (tok == "opengl") {
            set_feature(Target::OpenGL);
        } else if (tok == "openglcompute") {
            set_feature(Target::OpenGLCompute);
        } else if (tok == "renderscript") {
            set_feature(Target::Renderscript);
        } else if (tok == "user_context") {
            set_feature(Target::UserContext);
        } else if (tok == "register_metadata") {
            set_feature(Target::RegisterMetadata);
        } else if (tok == "no_asserts") {
            set_feature(Target::NoAsserts);
        } else if (tok == "no_bounds_query") {
            set_feature(Target::NoBoundsQuery);
        } else if (tok == "cl_doubles") {
            set_feature(Target::CLDoubles);
        } else if (tok == "fma") {
            set_features({Target::FMA, Target::SSE41, Target::AVX});
        } else if (tok == "fma4") {
            set_features({Target::FMA4, Target::SSE41, Target::AVX});
        } else if (tok == "f16c") {
            set_features({Target::F16C, Target::SSE41, Target::AVX});
        } else if (tok == "matlab") {
            set_feature(Target::Matlab);
        } else if (tok == "profile") {
            set_feature(Target::Profile);
        } else if (tok == "no_runtime") {
            set_feature(Target::NoRuntime);
        } else {
            return false;
        }

        if (is_os) {
            if (os_specified) {
                return false;
            }
            os_specified = true;
        }

        if (is_arch) {
            if (arch_specified) {
                return false;
            }
            arch_specified = true;
        }

        if (is_bits) {
            if (bits_specified) {
                return false;
            }
            bits_specified = true;
        }
    }

    if (arch_specified && !bits_specified) {
        return false;
    }

    // If arch is PNaCl, require explicit setting of os and bits as well.
    if (arch_specified && arch == Target::PNaCl) {
        if (!os_specified || os != Target::NaCl) {
            return false;
        }
        if (!bits_specified || bits != 32) {
            return false;
        }
    }

    return true;
}

std::string Target::to_string() const {
  const char* const arch_names[] = {
      "arch_unknown", "x86", "arm", "pnacl", "mips", "hexagon"
  };
  const char* const os_names[] = {
      "os_unknown", "linux", "windows", "osx", "android", "ios", "nacl"
  };
  // The contents of this array must match Target::Features.
  const char* const feature_names[] = {
      "jit", "debug", "no_asserts", "no_bounds_query",
      "sse41", "avx", "avx2", "fma", "fma4", "f16c",
      "armv7s", "no_neon",
      "cuda", "cuda_capability_30", "cuda_capability_32", "cuda_capability_35", "cuda_capability_50",
      "opencl", "cl_doubles",
      "opengl", "openglcompute", "rs",
      "user_context",
      "hvx",
      "hvx-double",
      "register_metadata",
      "matlab",
      "profile",
      "no_runtime"
  };
  internal_assert(sizeof(feature_names) / sizeof(feature_names[0]) == FeatureEnd);
  string result = string(arch_names[arch])
      + "-" + std::to_string(bits)
      + "-" + string(os_names[os]);
  for (size_t i = 0; i < FeatureEnd; ++i) {
      if (has_feature(static_cast<Feature>(i))) {
          result += "-" + string(feature_names[i]);
      }
  }
  return result;
}

}

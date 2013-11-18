#include <iostream>
#include <string>

#include "Target.h"
#include "LLVM_Headers.h"
#include "Debug.h"
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
    // We save %ebx in case it's the PIC register
    __asm__ __volatile__ (
        "cpuid                 \n\t"
        "xchg{l}\t{%%}ebx, %1  \n\t"
        : "=a" (info[0]), "=r" (info[1]), "=c" (info[2]), "=d" (info[3])
        : "0" (infoType), "2" (extra)
        : "%rbx");
}
#else
static void cpuid(int info[4], int infoType, int extra) {
    // We save %ebx in case it's the PIC register
    __asm__ __volatile__ (
        "xchg{l}\t{%%}ebx, %1  \n\t"
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

    #ifdef __arm__
    Target::Arch arch = Target::ARM;
    return Target(os, arch, bits, 0);
    #else

    Target::Arch arch = Target::X86;

    int info[4];
    cpuid(info, 1, 0);
    bool have_sse41 = info[2] & (1 << 19);
    bool have_sse2 = info[3] & (1 << 26);
    bool have_avx = info[2] & (1 << 28);
    bool have_f16 = info[2] & (1 << 29);
    bool have_rdrand = info[2] & (1 << 30);

    assert(have_sse2 && "The x86 backend assumes at least sse2 support");

    uint64_t features = 0;
    if (have_sse41) features |= Target::SSE41;
    if (have_avx)   features |= Target::AVX;

    if (use_64_bits && have_avx && have_f16 && have_rdrand) {
        // So far, so good.  AVX2?
        // Call cpuid with eax=7, ecx=0
        int info2[4];
        cpuid(info2, 7, 0);
        bool have_avx2 = info[2] & (1 << 5);
        if (have_avx2) {
            features |= Target::AVX2;
        }
    }

    return Target(os, arch, bits, features);
#endif
}

Target get_target_from_environment() {

    //Internal::debug(0) << "Getting host target \n";

    Target t = get_host_target();

    //Internal::debug(0) << "Got host target \n";

    string target;
#ifdef _WIN32
    char buf[128];
    size_t read = 0;
    getenv_s(&read, buf, "HL_TARGET");
    if (read) {
        target = buf;
    } else {
        return t;
    }
#else
    char *buf = getenv("HL_TARGET");
    if (buf) {
        target = buf;
    } else {
        return t;
    }
#endif

    if (target.empty()) return t;

    uint64_t host_features = t.features;
    t.features = 0;

    // HL_TARGET should be arch-os-feature1-feature2...
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
            t.arch = Target::X86;
            is_arch = true;
        } else if (tok == "arm") {
            t.arch = Target::ARM;
            is_arch = true;
        } else if (tok == "32") {
            t.bits = 32;
            is_bits = true;
        } else if (tok == "64") {
            t.bits = 64;
            is_bits = true;
        } else if (tok == "linux") {
            t.os = Target::Linux;
            is_os = true;
        } else if (tok == "windows") {
            t.os = Target::Windows;
            is_os = true;
        } else if (tok == "nacl") {
            t.os = Target::NaCl;
            is_os = true;
        } else if (tok == "osx") {
            t.os = Target::OSX;
            is_os = true;
        } else if (tok == "android") {
            t.os = Target::Android;
            is_os = true;
        } else if (tok == "ios") {
            t.os = Target::IOS;
            is_os = true;
        } else if (tok == "host") {
            t.features |= host_features;
            is_os = true;
            is_arch = true;
            is_bits = true;
        } else if (tok == "sse41") {
            t.features |= Target::SSE41;
        } else if (tok == "avx") {
            t.features |= (Target::SSE41 | Target::AVX);
        } else if (tok == "avx2") {
            t.features |= (Target::SSE41 | Target::AVX | Target::AVX2);
        } else if (tok == "cuda" || tok == "ptx") {
            t.features |= Target::CUDA;
        } else if (tok == "opencl") {
            t.features |= Target::OpenCL;
        } else if (tok == "spir") {
            t.features |= Target::SPIR;
        } else if (tok == "gpu_debug") {
            t.features |= Target::GPUDebug;
        } else {
            std::cerr << "Did not understand HL_TARGET=" << target << "\n"
                      << "Expected format is arch-os-feature1-feature2-... "
                      << "Where arch is x86-32, x86-64, arm-32, arm-64, "
                      << "and os is linux, windows, osx, nacl, ios, or android. "
                      << "If arch or os are omitted, they default to the host. "
                      << "Features include sse41, avx, avx2, cuda, opencl, and gpu_debug.\n"
                      << "HL_TARGET can also include \"host\", which sets the "
                      << "host's architecture, os, and feature set, with the "
                      << "exception of the GPU runtimes, which default to off\n";

            assert(false);
        }

        if (is_os) {
            assert(!os_specified && "Target string specifies OS twice");
            os_specified = true;
        }

        if (is_arch) {
            assert(!arch_specified && "Target string specifies architecture twice");
            arch_specified = true;
        }

        if (is_bits) {
            assert(!bits_specified && "Target string specifies bits twice");
            bits_specified = true;
        }
    }

    if (arch_specified && !bits_specified) {
        std::cerr << "WARNING: If architecture is specified (e.g. \"arm\"), "
                  << "then bit width should also be specified (e.g. \"arm-32\"). "
                  << "In the future this will be an error. ";
        if (t.arch == Target::X86) {
            std::cerr << "For x86 we default to the bit width of the host: " << t.bits << "\n;";
        } else {
            std::cerr << "For this target we default to 32-bits\n";
            t.bits = 32;
        }
    }

    return t;
}

#define DECLARE_INITMOD(mod)                                            \
    extern "C" unsigned char halide_internal_initmod_##mod[];           \
    extern "C" int halide_internal_initmod_##mod##_length;              \
    llvm::Module *get_initmod_##mod(llvm::LLVMContext *context) {      \
        llvm::StringRef sb = llvm::StringRef((const char *)halide_internal_initmod_##mod, \
                                             halide_internal_initmod_##mod##_length); \
        llvm::MemoryBuffer *bitcode_buffer = llvm::MemoryBuffer::getMemBuffer(sb); \
        llvm::Module *module = llvm::ParseBitcodeFile(bitcode_buffer, *context); \
        delete bitcode_buffer;                                          \
        return module;                                                  \
    }

#define DECLARE_NO_INITMOD(mod)                                         \
    llvm::Module *get_initmod_##mod(LLVMContext *context) {             \
        assert(false && "Halide was compiled without support for this target\n"); \
        return NULL;                                                    \
    }

#define DECLARE_CPP_INITMOD(mod) \
    DECLARE_INITMOD(mod ## _32) \
    DECLARE_INITMOD(mod ## _64) \
    llvm::Module *get_initmod_##mod(llvm::LLVMContext *context, bool bits_64) { \
        if (bits_64) return get_initmod_##mod##_64(context);            \
        return get_initmod_##mod##_32(context);                         \
    }

#define DECLARE_LL_INITMOD(mod) \
    DECLARE_INITMOD(mod ## _ll)

DECLARE_CPP_INITMOD(android_clock)
DECLARE_CPP_INITMOD(android_host_cpu_count)
DECLARE_CPP_INITMOD(android_io)
DECLARE_CPP_INITMOD(ios_io)
DECLARE_CPP_INITMOD(cuda)
DECLARE_CPP_INITMOD(cuda_debug)
DECLARE_CPP_INITMOD(fake_thread_pool)
DECLARE_CPP_INITMOD(gcd_thread_pool)
DECLARE_CPP_INITMOD(linux_clock)
DECLARE_CPP_INITMOD(linux_host_cpu_count)
DECLARE_CPP_INITMOD(nogpu)
DECLARE_CPP_INITMOD(opencl)
DECLARE_CPP_INITMOD(opencl_debug)
DECLARE_CPP_INITMOD(osx_host_cpu_count)
DECLARE_CPP_INITMOD(osx_io)
DECLARE_CPP_INITMOD(posix_allocator)
DECLARE_CPP_INITMOD(posix_clock)
DECLARE_CPP_INITMOD(windows_clock)
DECLARE_CPP_INITMOD(osx_clock)
DECLARE_CPP_INITMOD(posix_error_handler)
DECLARE_CPP_INITMOD(posix_io)
DECLARE_CPP_INITMOD(nacl_io)
DECLARE_CPP_INITMOD(windows_io)
DECLARE_CPP_INITMOD(posix_math)
DECLARE_CPP_INITMOD(posix_thread_pool)
DECLARE_CPP_INITMOD(tracing)
DECLARE_CPP_INITMOD(write_debug_image)

DECLARE_LL_INITMOD(arm)
DECLARE_LL_INITMOD(posix_math)
DECLARE_LL_INITMOD(ptx_dev)
DECLARE_LL_INITMOD(spir_dev)
DECLARE_LL_INITMOD(x86_avx)
DECLARE_LL_INITMOD(x86)
DECLARE_LL_INITMOD(x86_sse41)

namespace Internal {

/** Create an llvm module containing the support code for a given target. */
llvm::Module *get_initial_module_for_target(Target t, llvm::LLVMContext *c) {

    assert(t.bits == 32 || t.bits == 64);
    bool bits_64 = (t.bits == 64);

    vector<llvm::Module *> modules;

    // OS-dependent modules
    if (t.os == Target::Linux) {
        modules.push_back(get_initmod_linux_clock(c, bits_64));
        modules.push_back(get_initmod_posix_io(c, bits_64));
        modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64));
    } else if (t.os == Target::OSX) {
        modules.push_back(get_initmod_osx_clock(c, bits_64));
        modules.push_back(get_initmod_osx_io(c, bits_64));
        modules.push_back(get_initmod_gcd_thread_pool(c, bits_64));
    } else if (t.os == Target::Android) {
        modules.push_back(get_initmod_android_clock(c, bits_64));
        modules.push_back(get_initmod_android_io(c, bits_64));
        modules.push_back(get_initmod_android_host_cpu_count(c, bits_64));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64));
    } else if (t.os == Target::Windows) {
        modules.push_back(get_initmod_windows_clock(c, bits_64));
        modules.push_back(get_initmod_windows_io(c, bits_64));
        modules.push_back(get_initmod_fake_thread_pool(c, bits_64));
    } else if (t.os == Target::IOS) {
        modules.push_back(get_initmod_posix_clock(c, bits_64));
        modules.push_back(get_initmod_ios_io(c, bits_64));
        modules.push_back(get_initmod_gcd_thread_pool(c, bits_64));
    } else if (t.os == Target::NaCl) {
        modules.push_back(get_initmod_posix_clock(c, bits_64));
        modules.push_back(get_initmod_nacl_io(c, bits_64));
        modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64));
    }

    // These modules are always used
    modules.push_back(get_initmod_posix_math(c, bits_64));
    modules.push_back(get_initmod_posix_math_ll(c));
    modules.push_back(get_initmod_tracing(c, bits_64));
    modules.push_back(get_initmod_write_debug_image(c, bits_64));
    modules.push_back(get_initmod_posix_allocator(c, bits_64));
    modules.push_back(get_initmod_posix_error_handler(c, bits_64));

    // These modules are optional
    if (t.arch == Target::X86) {
        modules.push_back(get_initmod_x86_ll(c));
    }
    if (t.arch == Target::ARM) {
        modules.push_back(get_initmod_arm_ll(c));
    }
    if (t.features & Target::SSE41) {
        modules.push_back(get_initmod_x86_sse41_ll(c));
    }
    if (t.features & Target::AVX) {
        modules.push_back(get_initmod_x86_avx_ll(c));
    }
    if (t.features & Target::CUDA) {
        if (t.features & Target::GPUDebug) {
            modules.push_back(get_initmod_cuda_debug(c, bits_64));
        } else {
            modules.push_back(get_initmod_cuda(c, bits_64));
        }
    } else if (t.features & (Target::OpenCL | Target::SPIR)) {
        if (t.features & Target::GPUDebug) {
            modules.push_back(get_initmod_opencl_debug(c, bits_64));
        } else {
            modules.push_back(get_initmod_opencl(c, bits_64));
        }
    } else {
        modules.push_back(get_initmod_nogpu(c, bits_64));
    }

    // Link them all together
    for (size_t i = 1; i < modules.size(); i++) {
        string err_msg;
        bool failed = llvm::Linker::LinkModules(modules[0], modules[i],
                                                llvm::Linker::DestroySource, &err_msg);
        if (failed) {
            std::cerr << "Failure linking initial modules: " << err_msg << "\n";
            assert(false);
        }
    }

    // Now remark most weak symbols as linkonce. They are only weak to
    // prevent llvm from stripping them during initial module
    // assembly. This means they can be stripped later.

    // The symbols that we actually might want to override as a user
    // must remain weak.
    string retain[] = {"halide_copy_to_host",
                       "halide_copy_to_dev",
                       "halide_dev_free",
                       "halide_set_error_handler",
                       "halide_set_custom_allocator",
                       "halide_set_custom_trace",
                       "halide_set_custom_do_par_for",
                       "halide_set_custom_do_task",
                       "halide_shutdown_thread_pool",
                       "halide_shutdown_trace",
                       "halide_set_cuda_context",
                       "halide_release",
                       "halide_current_time_ns",
                       "halide_host_cpu_count",
                       ""};

    llvm::Module *module = modules[0];

    for (llvm::Module::iterator iter = module->begin(); iter != module->end(); iter++) {
        llvm::Function *f = (llvm::Function *)(iter);
        bool can_strip = true;
        for (size_t i = 0; !retain[i].empty(); i++) {
            if (f->getName() == retain[i]) {
                can_strip = false;
            }
        }

        if (can_strip) {
            llvm::GlobalValue::LinkageTypes t = f->getLinkage();
            if (t == llvm::GlobalValue::WeakAnyLinkage) {
                f->setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
            } else if (t == llvm::GlobalValue::WeakODRLinkage) {
                f->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
            }
        }

    }

    // Now remove the force-usage global that prevented clang from
    // dropping functions from the initial module.
    llvm::GlobalValue *llvm_used = module->getNamedGlobal("llvm.used");
    if (llvm_used) {
        llvm_used->eraseFromParent();
    }

    return module;
}

llvm::Module *get_initial_module_for_ptx_device(llvm::LLVMContext *c) {
    return get_initmod_ptx_dev_ll(c);
}

llvm::Module *get_initial_module_for_spir_device(llvm::LLVMContext *c) {
    return get_initmod_spir_dev_ll(c);
}

}

}

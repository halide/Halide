#include <iostream>
#include <string>
#include <memory>

#include "Target.h"
#include "Debug.h"
#include "Error.h"
#include "LLVM_Headers.h"
#include "Util.h"

namespace Halide {

using std::string;
using std::vector;

using Internal::vec;

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
    string target = get_env("HL_JIT_TARGET");
    if (target.empty()) {
        return host;
    } else {
        Target t = parse_target_string(target);
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
            set_features(vec(Target::SSE41, Target::AVX));
        } else if (tok == "avx2") {
            set_features(vec(Target::SSE41, Target::AVX, Target::AVX2));
        } else if (tok == "armv7s") {
            set_feature(Target::ARMv7s);
        } else if (tok == "cuda" || tok == "ptx") {
            set_feature(Target::CUDA);
        } else if (tok == "cuda_capability_30") {
            set_features(vec(Target::CUDA, Target::CUDACapability30));
        } else if (tok == "cuda_capability_32") {
            set_features(vec(Target::CUDA, Target::CUDACapability32));
        } else if (tok == "cuda_capability_35") {
            set_features(vec(Target::CUDA, Target::CUDACapability35));
        } else if (tok == "cuda_capability_50") {
            set_features(vec(Target::CUDA, Target::CUDACapability50));
        } else if (tok == "opencl") {
            set_feature(Target::OpenCL);
        } else if (tok == "debug" || tok == "gpu_debug") {
            set_feature(Target::Debug);
        } else if (tok == "opengl") {
            set_feature(Target::OpenGL);
        } else if (tok == "user_context") {
            set_feature(Target::UserContext);
        } else if (tok == "no_asserts") {
            set_feature(Target::NoAsserts);
        } else if (tok == "no_bounds_query") {
            set_feature(Target::NoBoundsQuery);
        } else if (tok == "cl_doubles") {
            set_feature(Target::CLDoubles);
        } else if (tok == "fma") {
            set_features(vec(Target::FMA, Target::SSE41, Target::AVX));
        } else if (tok == "fma4") {
            set_features(vec(Target::FMA4, Target::SSE41, Target::AVX));
        } else if (tok == "f16c") {
            set_features(vec(Target::F16C, Target::SSE41, Target::AVX));
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
      "arch_unknown", "x86", "arm", "pnacl", "mips"
  };
  const char* const os_names[] = {
      "os_unknown", "linux", "windows", "osx", "android", "ios", "nacl"
  };
  // The contents of this array must match Target::Features.
  const char* const feature_names[] = {
      "jit", "debug", "no_asserts", "no_bounds_query",
      "sse41", "avx", "avx2", "fma", "fma4", "f16c",
      "armv7s",
      "cuda", "cuda_capability_30", "cuda_capability_32", "cuda_capability_35", "cuda_capability_50",
      "opencl", "cl_doubles",
      "opengl",
      "user_context"
  };
  internal_assert(sizeof(feature_names) / sizeof(feature_names[0]) == FeatureEnd);
  string result = string(arch_names[arch])
      + "-" + Internal::int_to_string(bits)
      + "-" + string(os_names[os]);
  for (size_t i = 0; i < FeatureEnd; ++i) {
      if (has_feature(static_cast<Feature>(i))) {
          result += "-" + string(feature_names[i]);
      }
  }
  return result;
}

namespace {
llvm::Module *parse_bitcode_file(llvm::StringRef buf, llvm::LLVMContext *context, const char *id) {

    #if LLVM_VERSION >= 36
    llvm::MemoryBufferRef bitcode_buffer = llvm::MemoryBufferRef(buf, id);
    #else
    llvm::MemoryBuffer *bitcode_buffer = llvm::MemoryBuffer::getMemBuffer(buf);
    #endif

    #if LLVM_VERSION >= 35
    llvm::Module *mod = llvm::parseBitcodeFile(bitcode_buffer, *context).get();
    #else
    llvm::Module *mod = llvm::ParseBitcodeFile(bitcode_buffer, *context);
    #endif

    #if LLVM_VERSION < 36
    delete bitcode_buffer;
    #endif

    mod->setModuleIdentifier(id);

    return mod;
}
}

#define DECLARE_INITMOD(mod)                                            \
    extern "C" unsigned char halide_internal_initmod_##mod[];           \
    extern "C" int halide_internal_initmod_##mod##_length;              \
    llvm::Module *get_initmod_##mod(llvm::LLVMContext *context) {      \
        llvm::StringRef sb = llvm::StringRef((const char *)halide_internal_initmod_##mod, \
                                             halide_internal_initmod_##mod##_length); \
        llvm::Module *module = parse_bitcode_file(sb, context, #mod);   \
        return module;                                                  \
    }

#define DECLARE_NO_INITMOD(mod)                                         \
    llvm::Module *get_initmod_##mod(llvm::LLVMContext *, bool) {             \
        user_error << "Halide was compiled without support for this target\n"; \
        return NULL;                                                    \
    }                                                                   \
    llvm::Module *get_initmod_##mod##_ll(llvm::LLVMContext *) {         \
        user_error << "Halide was compiled without support for this target\n"; \
        return NULL;                                                    \
    }

#define DECLARE_CPP_INITMOD(mod) \
    DECLARE_INITMOD(mod ## _32_debug) \
    DECLARE_INITMOD(mod ## _64_debug) \
    DECLARE_INITMOD(mod ## _32) \
    DECLARE_INITMOD(mod ## _64) \
    llvm::Module *get_initmod_##mod(llvm::LLVMContext *context, bool bits_64, bool debug) { \
        if (bits_64) {                                                                      \
            if (debug) return get_initmod_##mod##_64_debug(context);                        \
            else return get_initmod_##mod##_64(context);                                    \
        } else {                                                                            \
            if (debug) return get_initmod_##mod##_32_debug(context);                        \
            else return get_initmod_##mod##_32(context);                                    \
        }                                                                                   \
    }

#define DECLARE_LL_INITMOD(mod) \
    DECLARE_INITMOD(mod ## _ll)

DECLARE_CPP_INITMOD(android_clock)
DECLARE_CPP_INITMOD(android_host_cpu_count)
DECLARE_CPP_INITMOD(android_io)
DECLARE_CPP_INITMOD(android_opengl_context)
DECLARE_CPP_INITMOD(ios_io)
DECLARE_CPP_INITMOD(cuda)
DECLARE_CPP_INITMOD(windows_cuda)
DECLARE_CPP_INITMOD(fake_thread_pool)
DECLARE_CPP_INITMOD(gcd_thread_pool)
DECLARE_CPP_INITMOD(linux_clock)
DECLARE_CPP_INITMOD(linux_host_cpu_count)
DECLARE_CPP_INITMOD(linux_opengl_context)
DECLARE_CPP_INITMOD(osx_opengl_context)
DECLARE_CPP_INITMOD(nogpu)
DECLARE_CPP_INITMOD(opencl)
DECLARE_CPP_INITMOD(windows_opencl)
DECLARE_CPP_INITMOD(opengl)
DECLARE_CPP_INITMOD(osx_host_cpu_count)
DECLARE_CPP_INITMOD(posix_allocator)
DECLARE_CPP_INITMOD(posix_clock)
DECLARE_CPP_INITMOD(windows_clock)
DECLARE_CPP_INITMOD(osx_clock)
DECLARE_CPP_INITMOD(posix_error_handler)
DECLARE_CPP_INITMOD(posix_io)
DECLARE_CPP_INITMOD(ssp)
DECLARE_CPP_INITMOD(windows_io)
DECLARE_CPP_INITMOD(posix_math)
DECLARE_CPP_INITMOD(posix_thread_pool)
DECLARE_CPP_INITMOD(windows_thread_pool)
DECLARE_CPP_INITMOD(tracing)
DECLARE_CPP_INITMOD(write_debug_image)
DECLARE_CPP_INITMOD(posix_print)
DECLARE_CPP_INITMOD(gpu_device_selection)
DECLARE_CPP_INITMOD(cache)
DECLARE_CPP_INITMOD(nacl_host_cpu_count)
DECLARE_CPP_INITMOD(to_string)

#ifdef WITH_ARM
DECLARE_LL_INITMOD(arm)
#else
DECLARE_NO_INITMOD(arm)
#endif
#ifdef WITH_AARCH64
DECLARE_LL_INITMOD(aarch64)
#else
DECLARE_NO_INITMOD(aarch64)
#endif
DECLARE_LL_INITMOD(posix_math)
DECLARE_LL_INITMOD(pnacl_math)
DECLARE_LL_INITMOD(win32_math)
DECLARE_LL_INITMOD(ptx_dev)
#ifdef WITH_PTX
DECLARE_LL_INITMOD(ptx_compute_20)
DECLARE_LL_INITMOD(ptx_compute_30)
DECLARE_LL_INITMOD(ptx_compute_35)
#endif
#ifdef WITH_X86
DECLARE_LL_INITMOD(x86_avx)
DECLARE_LL_INITMOD(x86)
DECLARE_LL_INITMOD(x86_sse41)
#else
DECLARE_NO_INITMOD(x86_avx)
DECLARE_NO_INITMOD(x86)
DECLARE_NO_INITMOD(x86_sse41)
#endif
#ifdef WITH_MIPS
DECLARE_LL_INITMOD(mips)
#else
DECLARE_NO_INITMOD(mips)
#endif

namespace {

// Link all modules together and with the result in modules[0],
// all other input modules are destroyed.
void link_modules(std::vector<llvm::Module *> &modules) {
    #if LLVM_VERSION >= 35
    // LLVM is moving to requiring data layouts to exist. Use the
    // datalayout of the first module that has one for all modules.
    const llvm::DataLayout *data_layout = NULL;
    for (size_t i = 0; data_layout == NULL && i < modules.size(); i++) {
        data_layout = modules[i]->getDataLayout();
    }

    // If LLVM is 3.5 or greater, we have C++11.
    std::unique_ptr<llvm::DataLayout> default_layout;
    if (data_layout == NULL) {
        // An empty data layout is acceptable as a last ditch default.
        default_layout.reset(new llvm::DataLayout(""));
        data_layout = default_layout.get();
    }
    #endif

    // Link them all together
    for (size_t i = 1; i < modules.size(); i++) {
        #if LLVM_VERSION >= 35
        modules[i]->setDataLayout(data_layout);
        #endif
        // This is a workaround to silence some linkage warnings during
        // tests: normally all modules will have the same triple,
        // but on 64-bit targets some may have "x86_64-unknown-unknown-unknown"
        // as a workaround for -m64 requiring an explicit 64-bit target.
        modules[i]->setTargetTriple(modules[0]->getTargetTriple());
        string err_msg;
        #if LLVM_VERSION >= 36
        bool failed = llvm::Linker::LinkModules(modules[0], modules[i]);
        #else
        bool failed = llvm::Linker::LinkModules(modules[0], modules[i],
                                                llvm::Linker::DestroySource, &err_msg);
        #endif
        if (failed) {
            internal_error << "Failure linking initial modules: " << err_msg << "\n";
        }
    }

    // Now remark most weak symbols as linkonce. They are only weak to
    // prevent llvm from stripping them during initial module
    // assembly. This means they can be stripped later.

    // The symbols that we actually might want to override as a user
    // must remain weak.
    string retain[] = {"halide_copy_to_host",
                       "halide_copy_to_dev",
                       "halide_dev_malloc",
                       "halide_dev_free",
                       "halide_set_error_handler",
                       "halide_set_custom_allocator",
                       "halide_set_custom_trace",
                       "halide_set_custom_do_par_for",
                       "halide_set_custom_do_task",
                       "halide_shutdown_thread_pool",
                       "halide_shutdown_trace",
                       "halide_set_trace_file",
                       "halide_set_cuda_context",
                       "halide_set_cl_context",
                       "halide_dev_sync",
                       "halide_release",
                       "halide_current_time_ns",
                       "halide_host_cpu_count",
                       "halide_set_num_threads",
                       "halide_opengl_get_proc_address",
                       "halide_opengl_create_context",
                       "halide_opengl_output_client_bound",
                       "halide_opengl_context_lost",
                       "halide_set_custom_print",
                       "halide_print",
                       "halide_set_gpu_device",
                       "halide_set_ocl_platform_name",
                       "halide_set_ocl_device_type",
                       "halide_memoization_cache_set_size",
                       "halide_memoization_cache_lookup",
                       "halide_memoization_cache_store",
                       "halide_memoization_cache_release",
                       "halide_memoization_cache_cleanup",
                       "__stack_chk_guard",
                       "__stack_chk_fail",
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
}

}

namespace Internal {

/** When JIT-compiling on 32-bit windows, we need to rewrite calls
 *  to name-mangled win32 api calls to non-name-mangled versions.
 */
void undo_win32_name_mangling(llvm::Module *m) {
    llvm::IRBuilder<> builder(m->getContext());
    // For every function prototype...
    for (llvm::Module::iterator iter = m->begin(); iter != m->end(); ++iter) {
        llvm::Function *f = (llvm::Function *)(iter);
        string n = f->getName();
        // if it's a __stdcall call that starts with \01_, then we're making a win32 api call
        if (f->getCallingConv() == llvm::CallingConv::X86_StdCall &&
            f->empty() &&
            n.size() > 2 && n[0] == 1 && n[1] == '_') {

            // Unmangle the name.
            string unmangled_name = n.substr(2);
            size_t at = unmangled_name.rfind('@');
            unmangled_name = unmangled_name.substr(0, at);

            // Extern declare the unmangled version.
            llvm::Function *unmangled = llvm::Function::Create(f->getFunctionType(), f->getLinkage(), unmangled_name, m);
            unmangled->setCallingConv(f->getCallingConv());

            // Add a body to the mangled version that calls the unmangled version.
            llvm::BasicBlock *block = llvm::BasicBlock::Create(m->getContext(), "entry", f);
            builder.SetInsertPoint(block);

            vector<llvm::Value *> args;
            for (llvm::Function::arg_iterator iter = f->arg_begin();
                 iter != f->arg_end(); ++iter) {
                args.push_back(iter);
            }

            llvm::CallInst *c = builder.CreateCall(unmangled, args);
            c->setCallingConv(f->getCallingConv());

            if (f->getReturnType()->isVoidTy()) {
                builder.CreateRetVoid();
            } else {
                builder.CreateRet(c);
            }
        }
    }
}

void add_underscore_to_posix_call(llvm::CallInst *call, llvm::Function *fn, llvm::Module *m) {
    string new_name = "_" + fn->getName().str();
    llvm::Function *alt = m->getFunction(new_name);
    if (!alt) {
        alt = llvm::Function::Create(fn->getFunctionType(),
                                     llvm::GlobalValue::ExternalLinkage,
                                     new_name, m);
    }
    internal_assert(alt->getName() == new_name);
    call->setCalledFunction(alt);
}

/** Windows uses _close, _open, _write, etc instead of the posix
 * names. Defining stubs that redirect causes mis-compilations inside
 * of mcjit, so we just rewrite uses of these functions to include an
 * underscore. */
void add_underscores_to_posix_calls_on_windows(llvm::Module *m) {
    string posix_fns[] = {"vsnprintf", "open", "close", "write"};

    string *posix_fns_begin = posix_fns;
    string *posix_fns_end = posix_fns + sizeof(posix_fns) / sizeof(posix_fns[0]);

    for (llvm::Module::iterator iter = m->begin(); iter != m->end(); ++iter) {
        for (llvm::Function::iterator f_iter = iter->begin(); f_iter != iter->end(); ++f_iter) {
            for (llvm::BasicBlock::iterator b_iter = f_iter->begin(); b_iter != f_iter->end(); ++b_iter) {
                llvm::Value *inst = (llvm::Value *)b_iter;
                if (llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(inst)) {
                    if (llvm::Function *fn = call->getCalledFunction()) {
                        if (std::find(posix_fns_begin, posix_fns_end, fn->getName()) != posix_fns_end) {
                            add_underscore_to_posix_call(call, fn, m);
                        }
                    }
                }
            }
        }
    }
}

/** Create an llvm module containing the support code for a given target. */
llvm::Module *get_initial_module_for_target(Target t, llvm::LLVMContext *c) {

    internal_assert(t.bits == 32 || t.bits == 64);
    // NaCl always uses the 32-bit runtime modules, because pointers
    // and size_t are 32-bit in 64-bit NaCl, and that's the only way
    // in which the 32- and 64-bit runtimes differ.
    bool bits_64 = (t.bits == 64) && (t.os != Target::NaCl);
    bool debug = t.has_feature(Target::Debug);

#ifndef USE_MCJIT
    // -g (debug info) doesn't work with the old JIT, give an
    // intelligible reason why here.
    if (debug && t.has_feature(Target::JIT)) {
        Internal::debug(0) << "The debug runtime is not supported when using JIT on this platform.\n";
        debug = false;
    }
#endif

    vector<llvm::Module *> modules;

    // OS-dependent modules
    if (t.os == Target::Linux) {
        modules.push_back(get_initmod_linux_clock(c, bits_64, debug));
        modules.push_back(get_initmod_posix_io(c, bits_64, debug));
        modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64, debug));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64, debug));
    } else if (t.os == Target::OSX) {
        modules.push_back(get_initmod_osx_clock(c, bits_64, debug));
        modules.push_back(get_initmod_posix_io(c, bits_64, debug));
        modules.push_back(get_initmod_gcd_thread_pool(c, bits_64, debug));
    } else if (t.os == Target::Android) {
        modules.push_back(get_initmod_android_clock(c, bits_64, debug));
        modules.push_back(get_initmod_android_io(c, bits_64, debug));
        modules.push_back(get_initmod_android_host_cpu_count(c, bits_64, debug));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64, debug));
    } else if (t.os == Target::Windows) {
        modules.push_back(get_initmod_windows_clock(c, bits_64, debug));
        modules.push_back(get_initmod_windows_io(c, bits_64, debug));
        modules.push_back(get_initmod_windows_thread_pool(c, bits_64, debug));
    } else if (t.os == Target::IOS) {
        modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
        modules.push_back(get_initmod_ios_io(c, bits_64, debug));
        modules.push_back(get_initmod_gcd_thread_pool(c, bits_64, debug));
    } else if (t.os == Target::NaCl) {
        modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
        modules.push_back(get_initmod_posix_io(c, bits_64, debug));
        modules.push_back(get_initmod_nacl_host_cpu_count(c, bits_64, debug));
        modules.push_back(get_initmod_posix_thread_pool(c, bits_64, debug));
        modules.push_back(get_initmod_ssp(c, bits_64, debug));
    }

    // Math intrinsics vary slightly across platforms
    if (t.os == Target::Windows && t.bits == 32) {
        modules.push_back(get_initmod_win32_math_ll(c));
    } else if (t.arch == Target::PNaCl) {
        modules.push_back(get_initmod_pnacl_math_ll(c));
    } else {
        modules.push_back(get_initmod_posix_math_ll(c));
    }

    // These modules are always used
    modules.push_back(get_initmod_gpu_device_selection(c, bits_64, debug));
    modules.push_back(get_initmod_posix_math(c, bits_64, debug));
    modules.push_back(get_initmod_tracing(c, bits_64, debug));
    modules.push_back(get_initmod_write_debug_image(c, bits_64, debug));
    modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
    modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
    modules.push_back(get_initmod_posix_print(c, bits_64, debug));
    modules.push_back(get_initmod_cache(c, bits_64, debug));
    modules.push_back(get_initmod_to_string(c, bits_64, debug));

    // These modules are optional
    if (t.arch == Target::X86) {
        modules.push_back(get_initmod_x86_ll(c));
    }
    if (t.arch == Target::ARM) {
        if (t.bits == 64) {
          modules.push_back(get_initmod_aarch64_ll(c));
        } else {
          modules.push_back(get_initmod_arm_ll(c));
        }
    }
    if (t.arch == Target::MIPS) {
        modules.push_back(get_initmod_mips_ll(c));
    }
    if (t.has_feature(Target::SSE41)) {
        modules.push_back(get_initmod_x86_sse41_ll(c));
    }
    if (t.has_feature(Target::AVX)) {
        modules.push_back(get_initmod_x86_avx_ll(c));
    }
    if (t.has_feature(Target::CUDA)) {
        if (t.os == Target::Windows) {
            modules.push_back(get_initmod_windows_cuda(c, bits_64, debug));
        } else {
            modules.push_back(get_initmod_cuda(c, bits_64, debug));
        }
    } else if (t.has_feature(Target::OpenCL)) {
        if (t.os == Target::Windows) {
            modules.push_back(get_initmod_windows_opencl(c, bits_64, debug));
        } else {
            modules.push_back(get_initmod_opencl(c, bits_64, debug));
        }
    } else if (t.has_feature(Target::OpenGL)) {
        modules.push_back(get_initmod_opengl(c, bits_64, debug));
        if (t.os == Target::Linux) {
            modules.push_back(get_initmod_linux_opengl_context(c, bits_64, debug));
        } else if (t.os == Target::OSX) {
            modules.push_back(get_initmod_osx_opengl_context(c, bits_64, debug));
        } else if (t.os == Target::Android) {
            modules.push_back(get_initmod_android_opengl_context(c, bits_64, debug));
        } else {
            // You're on your own to provide definitions of halide_opengl_get_proc_address and halide_opengl_create_context
        }
    } else {
        modules.push_back(get_initmod_nogpu(c, bits_64, debug));
    }

    link_modules(modules);

    if (t.os == Target::Windows &&
        t.bits == 32 &&
        (t.has_feature(Target::JIT))) {
        undo_win32_name_mangling(modules[0]);
    }

    if (t.os == Target::Windows) {
        add_underscores_to_posix_calls_on_windows(modules[0]);
    }

    return modules[0];
}

#ifdef WITH_PTX
llvm::Module *get_initial_module_for_ptx_device(Target target, llvm::LLVMContext *c) {
    std::vector<llvm::Module *> modules;
    modules.push_back(get_initmod_ptx_dev_ll(c));

    llvm::Module *module;

    // This table is based on the guidance at:
    // http://docs.nvidia.com/cuda/libdevice-users-guide/basic-usage.html#linking-with-libdevice
    if (target.has_feature(Target::CUDACapability35)) {
        module = get_initmod_ptx_compute_35_ll(c);
    } else if (target.features_any_of(vec(Target::CUDACapability32,
                                          Target::CUDACapability50))) {
        // For some reason sm_32 and sm_50 use libdevice 20
        module = get_initmod_ptx_compute_20_ll(c);
    } else if (target.has_feature(Target::CUDACapability30)) {
        module = get_initmod_ptx_compute_30_ll(c);
    } else {
        module = get_initmod_ptx_compute_20_ll(c);
    }
    modules.push_back(module);

    link_modules(modules);

    // For now, the PTX backend does not handle calling functions. So mark all functions
    // AvailableExternally to ensure they are inlined or deleted.
    for (llvm::Module::iterator iter = modules[0]->begin(); iter != modules[0]->end(); iter++) {
        llvm::Function *f = (llvm::Function *)(iter);

        // This is intended to set all definitions (not extern declarations)
        // to "available externally" which should guarantee they do not exist
        // after the resulting module is finalized to code. That is they must
        // be inlined to be used.
        //
        // However libdevice has a few routines that are marked
        // "noinline" which must either be changed to alow inlining or
        // preserved in generated code. This preserves the intent of
        // keeping these routines out-of-line and hence called by
        // not marking them AvailableExternally.

        if (!f->isDeclaration() && !f->hasFnAttribute(llvm::Attribute::NoInline)) {
            f->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
        }

        // Also mark the halide_gpu_thread_barrier as noduplicate.
        #if LLVM_VERSION > 32
        if (f->getName() == "halide_gpu_thread_barrier") {
            f->addFnAttr(llvm::Attribute::NoDuplicate);
        }
        #endif
    }

    return modules[0];
}
#endif

}

}

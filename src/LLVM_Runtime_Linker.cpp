#include "LLVM_Runtime_Linker.h"
#include "LLVM_Headers.h"

namespace Halide {

using std::string;
using std::vector;

namespace {

std::unique_ptr<llvm::Module> parse_bitcode_file(llvm::StringRef buf, llvm::LLVMContext *context, const char *id) {

    llvm::MemoryBufferRef bitcode_buffer = llvm::MemoryBufferRef(buf, id);

    auto ret_val = llvm::expectedToErrorOr(
        llvm::parseBitcodeFile(bitcode_buffer, *context));
    if (!ret_val) {
        internal_error << "Could not parse built-in bitcode file " << id
                       << " llvm error is " << ret_val.getError() << "\n";
    }

    std::unique_ptr<llvm::Module> result(std::move(*ret_val));
    result->setModuleIdentifier(id);

    return result;
}

}  // namespace

#define DECLARE_INITMOD(mod)                                                              \
    extern "C" unsigned char halide_internal_initmod_##mod[];                             \
    extern "C" int halide_internal_initmod_##mod##_length;                                \
    std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *context) {         \
        llvm::StringRef sb = llvm::StringRef((const char *)halide_internal_initmod_##mod, \
                                             halide_internal_initmod_##mod##_length);     \
        return parse_bitcode_file(sb, context, #mod);                                    \
    }

#define DECLARE_NO_INITMOD(mod)                                                      \
  std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *, bool, bool) { \
        user_error << "Halide was compiled without support for this target\n";       \
        return std::unique_ptr<llvm::Module>();                                      \
    }                                                                                \
  std::unique_ptr<llvm::Module> get_initmod_##mod##_ll(llvm::LLVMContext *) {        \
        user_error << "Halide was compiled without support for this target\n";       \
        return std::unique_ptr<llvm::Module>();                                      \
    }

#define DECLARE_CPP_INITMOD(mod) \
    DECLARE_INITMOD(mod ## _32_debug) \
    DECLARE_INITMOD(mod ## _64_debug) \
    DECLARE_INITMOD(mod ## _32) \
    DECLARE_INITMOD(mod ## _64) \
    std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *context, bool bits_64, bool debug) { \
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

// Universal CPP Initmods. Please keep sorted alphabetically.
DECLARE_CPP_INITMOD(alignment_128)
DECLARE_CPP_INITMOD(alignment_32)
DECLARE_CPP_INITMOD(android_clock)
DECLARE_CPP_INITMOD(android_host_cpu_count)
DECLARE_CPP_INITMOD(android_io)
DECLARE_CPP_INITMOD(android_opengl_context)
DECLARE_CPP_INITMOD(android_tempfile)
DECLARE_CPP_INITMOD(buffer_t)
DECLARE_CPP_INITMOD(cache)
DECLARE_CPP_INITMOD(can_use_target)
DECLARE_CPP_INITMOD(cuda)
DECLARE_CPP_INITMOD(destructors)
DECLARE_CPP_INITMOD(device_interface)
DECLARE_CPP_INITMOD(errors)
DECLARE_CPP_INITMOD(fake_thread_pool)
DECLARE_CPP_INITMOD(float16_t)
DECLARE_CPP_INITMOD(gpu_device_selection)
DECLARE_CPP_INITMOD(hexagon_dma)
DECLARE_CPP_INITMOD(hexagon_host)
DECLARE_CPP_INITMOD(ios_io)
DECLARE_CPP_INITMOD(linux_clock)
DECLARE_CPP_INITMOD(linux_host_cpu_count)
DECLARE_CPP_INITMOD(linux_opengl_context)
DECLARE_CPP_INITMOD(linux_yield)
DECLARE_CPP_INITMOD(matlab)
DECLARE_CPP_INITMOD(metadata)
DECLARE_CPP_INITMOD(mingw_math)
DECLARE_CPP_INITMOD(module_aot_ref_count)
DECLARE_CPP_INITMOD(module_jit_ref_count)
DECLARE_CPP_INITMOD(msan)
DECLARE_CPP_INITMOD(msan_stubs)
DECLARE_CPP_INITMOD(old_buffer_t)
DECLARE_CPP_INITMOD(opencl)
DECLARE_CPP_INITMOD(opengl)
DECLARE_CPP_INITMOD(openglcompute)
DECLARE_CPP_INITMOD(osx_clock)
DECLARE_CPP_INITMOD(osx_get_symbol)
DECLARE_CPP_INITMOD(osx_host_cpu_count)
DECLARE_CPP_INITMOD(osx_opengl_context)
DECLARE_CPP_INITMOD(osx_yield)
DECLARE_CPP_INITMOD(posix_allocator)
DECLARE_CPP_INITMOD(posix_clock)
DECLARE_CPP_INITMOD(posix_error_handler)
DECLARE_CPP_INITMOD(posix_get_symbol)
DECLARE_CPP_INITMOD(posix_io)
DECLARE_CPP_INITMOD(posix_tempfile)
DECLARE_CPP_INITMOD(posix_print)
DECLARE_CPP_INITMOD(posix_threads)
DECLARE_CPP_INITMOD(posix_threads_tsan)
DECLARE_CPP_INITMOD(prefetch)
DECLARE_CPP_INITMOD(profiler)
DECLARE_CPP_INITMOD(profiler_inlined)
DECLARE_CPP_INITMOD(qurt_allocator)
DECLARE_CPP_INITMOD(default_cache_allocator) 
DECLARE_CPP_INITMOD(hexagon_cache_allocator)
DECLARE_CPP_INITMOD(qurt_hvx)
DECLARE_CPP_INITMOD(qurt_init_fini)
DECLARE_CPP_INITMOD(qurt_threads)
DECLARE_CPP_INITMOD(qurt_threads_tsan)
DECLARE_CPP_INITMOD(qurt_yield)
DECLARE_CPP_INITMOD(runtime_api)
DECLARE_CPP_INITMOD(ssp)
DECLARE_CPP_INITMOD(to_string)
DECLARE_CPP_INITMOD(tracing)
DECLARE_CPP_INITMOD(windows_clock)
DECLARE_CPP_INITMOD(windows_cuda)
DECLARE_CPP_INITMOD(windows_get_symbol)
DECLARE_CPP_INITMOD(windows_io)
DECLARE_CPP_INITMOD(windows_opencl)
DECLARE_CPP_INITMOD(windows_profiler)
DECLARE_CPP_INITMOD(windows_tempfile)
DECLARE_CPP_INITMOD(windows_threads)
DECLARE_CPP_INITMOD(windows_threads_tsan)
DECLARE_CPP_INITMOD(windows_yield)
DECLARE_CPP_INITMOD(write_debug_image)

// Universal LL Initmods. Please keep sorted alphabetically.
DECLARE_LL_INITMOD(posix_math)
DECLARE_LL_INITMOD(win32_math)
DECLARE_LL_INITMOD(ptx_dev)

// Various conditional initmods follow (both LL and CPP).
#ifdef WITH_METAL
DECLARE_CPP_INITMOD(metal)
#ifdef WITH_ARM
DECLARE_CPP_INITMOD(metal_objc_arm)
#else
DECLARE_NO_INITMOD(metal_objc_arm)
#endif
#ifdef WITH_X86
DECLARE_CPP_INITMOD(metal_objc_x86)
#else
DECLARE_NO_INITMOD(metal_objc_x86)
#endif
#else
DECLARE_NO_INITMOD(metal)
DECLARE_NO_INITMOD(metal_objc_arm)
DECLARE_NO_INITMOD(metal_objc_x86)
#endif  // WITH_METAL

#ifdef WITH_ARM
DECLARE_LL_INITMOD(arm)
DECLARE_LL_INITMOD(arm_no_neon)
DECLARE_CPP_INITMOD(arm_cpu_features)
#else
DECLARE_NO_INITMOD(arm)
DECLARE_NO_INITMOD(arm_no_neon)
DECLARE_NO_INITMOD(arm_cpu_features)
#endif  // WITH_ARM

#ifdef WITH_AARCH64
DECLARE_LL_INITMOD(aarch64)
DECLARE_CPP_INITMOD(aarch64_cpu_features)
#else
DECLARE_NO_INITMOD(aarch64)
DECLARE_NO_INITMOD(aarch64_cpu_features)
#endif  // WITH_AARCH64

#ifdef WITH_PTX
DECLARE_LL_INITMOD(ptx_compute_20)
DECLARE_LL_INITMOD(ptx_compute_30)
DECLARE_LL_INITMOD(ptx_compute_35)
#endif  // WITH_PTX

#ifdef WITH_X86
DECLARE_LL_INITMOD(x86_avx2)
DECLARE_LL_INITMOD(x86_avx)
DECLARE_LL_INITMOD(x86)
DECLARE_LL_INITMOD(x86_sse41)
DECLARE_CPP_INITMOD(x86_cpu_features)
#else
DECLARE_NO_INITMOD(x86_avx2)
DECLARE_NO_INITMOD(x86_avx)
DECLARE_NO_INITMOD(x86)
DECLARE_NO_INITMOD(x86_sse41)
DECLARE_NO_INITMOD(x86_cpu_features)
#endif  // WITH_X86

#ifdef WITH_MIPS
DECLARE_LL_INITMOD(mips)
DECLARE_CPP_INITMOD(mips_cpu_features)
#else
DECLARE_NO_INITMOD(mips)
DECLARE_NO_INITMOD(mips_cpu_features)
#endif  // WITH_MIPS

#ifdef WITH_POWERPC
DECLARE_LL_INITMOD(powerpc)
DECLARE_CPP_INITMOD(powerpc_cpu_features)
#else
DECLARE_NO_INITMOD(powerpc)
DECLARE_NO_INITMOD(powerpc_cpu_features)
#endif  // WITH_POWERPC

#ifdef WITH_HEXAGON
DECLARE_LL_INITMOD(hvx_64)
DECLARE_LL_INITMOD(hvx_128)
DECLARE_CPP_INITMOD(hexagon_cpu_features)
#else
DECLARE_NO_INITMOD(hvx_64)
DECLARE_NO_INITMOD(hvx_128)
DECLARE_NO_INITMOD(hexagon_cpu_features)
#endif  // WITH_HEXAGON

namespace {

llvm::DataLayout get_data_layout_for_target(Target target) {
    if (target.arch == Target::X86) {
        if (target.bits == 32) {
            if (target.os == Target::OSX) {
                return llvm::DataLayout("e-m:o-p:32:32-f64:32:64-f80:128-n8:16:32-S128");
            } else if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-p:32:32-f64:32:64-f80:128-n8:16:32-S128");
            } else if (target.os == Target::Windows && !target.has_feature(Target::JIT)) {
                return llvm::DataLayout("e-m:x-p:32:32-i64:64-f80:32-n8:16:32-a:0:32-S32");
            } else if (target.os == Target::Windows) {
                return llvm::DataLayout("e-m:e-p:32:32-i64:64-f80:32-n8:16:32-a:0:32-S32");
            } else {
                // Linux/Android
                return llvm::DataLayout("e-m:e-p:32:32-f64:32:64-f80:32-n8:16:32-S128");
            }
        } else { // 64-bit
            if (target.os == Target::OSX) {
                return llvm::DataLayout("e-m:o-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::IOS) {
               return llvm::DataLayout("e-m:o-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::Windows && !target.has_feature(Target::JIT)) {
                return llvm::DataLayout("e-m:w-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::Windows) {
               return llvm::DataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
            } else {
                return llvm::DataLayout("e-m:e-i64:64-f80:128-n8:16:32:64-S128");
            }
        }
    } else if (target.arch == Target::ARM) {
        if (target.bits == 32) {
            if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-p:32:32-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32");
            } else {
                return llvm::DataLayout("e-m:e-p:32:32-i64:64-v128:64:128-a:0:32-n32-S64");
            }
        } else { // 64-bit
            if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-i64:64-i128:128-n32:64-S128");
            } else {
                return llvm::DataLayout("e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128");
            }
        }
    } else if (target.arch == Target::MIPS) {
        if (target.bits == 32) {
            return llvm::DataLayout("e-m:m-p:32:32-i8:8:32-i16:16:32-i64:64-n32-S64");
        } else {
            return llvm::DataLayout("e-m:m-i8:8:32-i16:16:32-i64:64-n32:64-S128");
        }
    } else if (target.arch == Target::POWERPC) {
        if (target.bits == 32) {
            return llvm::DataLayout("e-m:e-i32:32-n32");
        } else {
            return llvm::DataLayout("e-m:e-i64:64-n32:64");
        }
    } else if (target.arch == Target::Hexagon) {
        return llvm::DataLayout(
            "e-m:e-p:32:32:32-a:0-n16:32-i64:64:64-i32:32:32-i16:16:16-i1:8:8"
            "-f32:32:32-f64:64:64-v32:32:32-v64:64:64-v512:512:512-v1024:1024:1024-v2048:2048:2048");
    } else {
        internal_error << "Bad target arch: " << target.arch << "\n";
        return llvm::DataLayout("unreachable");
    }
}

}  // namespace

namespace Internal {

llvm::Triple get_triple_for_target(const Target &target) {
    llvm::Triple triple;

    if (target.arch == Target::X86) {
        if (target.bits == 32) {
            triple.setArch(llvm::Triple::x86);
        } else {
            user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
            triple.setArch(llvm::Triple::x86_64);
        }

        if (target.os == Target::Linux) {
            triple.setOS(llvm::Triple::Linux);
            triple.setEnvironment(llvm::Triple::GNU);
        } else if (target.os == Target::OSX) {
            triple.setVendor(llvm::Triple::Apple);
            triple.setOS(llvm::Triple::MacOSX);
        } else if (target.os == Target::Windows) {
            triple.setVendor(llvm::Triple::PC);
            triple.setOS(llvm::Triple::Win32);
            if (target.has_feature(Target::MinGW)) {
                triple.setEnvironment(llvm::Triple::GNU);
            } else {
                triple.setEnvironment(llvm::Triple::MSVC);
            }
            if (target.has_feature(Target::JIT)) {
                // Use ELF for jitting
                triple.setObjectFormat(llvm::Triple::ELF);
            }
        } else if (target.os == Target::Android) {
            triple.setOS(llvm::Triple::Linux);
            triple.setEnvironment(llvm::Triple::Android);
        } else if (target.os == Target::IOS) {
            // X86 on iOS for the simulator
            triple.setVendor(llvm::Triple::Apple);
            triple.setOS(llvm::Triple::IOS);
        }
    } else if (target.arch == Target::ARM) {
        if (target.bits == 32) {
            if (target.has_feature(Target::ARMv7s)) {
                triple.setArchName("armv7s");
            } else {
                triple.setArch(llvm::Triple::arm);
            }
        } else {
            user_assert(target.bits == 64) << "Target bits must be 32 or 64\n";
            #if (WITH_AARCH64)
            triple.setArch(llvm::Triple::aarch64);
            #else
            user_error << "AArch64 llvm target not enabled in this build of Halide\n";
            #endif
        }

        if (target.os == Target::Android) {
            triple.setOS(llvm::Triple::Linux);
            triple.setEnvironment(llvm::Triple::EABI);
        } else if (target.os == Target::IOS) {
            triple.setOS(llvm::Triple::IOS);
            triple.setVendor(llvm::Triple::Apple);
        } else if (target.os == Target::Linux) {
            triple.setOS(llvm::Triple::Linux);
            triple.setEnvironment(llvm::Triple::GNUEABIHF);
        } else {
            user_error << "No arm support for this OS\n";
        }
    } else if (target.arch == Target::MIPS) {
        // Currently MIPS support is only little-endian.
        if (target.bits == 32) {
            triple.setArch(llvm::Triple::mipsel);
        } else {
            user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
            triple.setArch(llvm::Triple::mips64el);
        }

        if (target.os == Target::Android) {
            triple.setOS(llvm::Triple::Linux);
            triple.setEnvironment(llvm::Triple::Android);
        } else {
            user_error << "No mips support for this OS\n";
        }
    } else if (target.arch == Target::POWERPC) {
        #if (WITH_POWERPC)
        // Only ppc*-unknown-linux-gnu are supported for the time being.
        user_assert(target.os == Target::Linux) << "PowerPC target is Linux-only.\n";
        triple.setVendor(llvm::Triple::UnknownVendor);
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::GNU);
        if (target.bits == 32) {
            triple.setArch(llvm::Triple::ppc);
        } else {
            // Currently POWERPC64 support is only little-endian.
            user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
            triple.setArch(llvm::Triple::ppc64le);
        }
        #else
        user_error << "PowerPC llvm target not enabled in this build of Halide\n";
        #endif
    } else if (target.arch == Target::Hexagon) {
        triple.setVendor(llvm::Triple::UnknownVendor);
        triple.setArch(llvm::Triple::hexagon);
        triple.setObjectFormat(llvm::Triple::ELF);
    } else {
        internal_error << "Bad target arch: " << target.arch << "\n";
    }

    return triple;
}

}  // namespace Internal

namespace {

// Link all modules together and with the result in modules[0], all
// other input modules are destroyed. Sets the datalayout and target
// triple appropriately for the target.
void link_modules(std::vector<std::unique_ptr<llvm::Module>> &modules, Target t) {

    llvm::DataLayout data_layout = get_data_layout_for_target(t);
    llvm::Triple triple = Internal::get_triple_for_target(t);

    // Set the layout and triple on the modules before linking, so
    // llvm doesn't complain while combining them.
    for (size_t i = 0; i < modules.size(); i++) {
        modules[i]->setDataLayout(data_layout);
        modules[i]->setTargetTriple(triple.str());
    }

    // Link them all together
    for (size_t i = 1; i < modules.size(); i++) {
        bool failed = llvm::Linker::linkModules(*modules[0],
                                                std::move(modules[i]));
        if (failed) {
            internal_error << "Failure linking initial modules\n";
        }
    }

    // Now remark most weak symbols as linkonce. They are only weak to
    // prevent llvm from stripping them during initial module
    // assembly. This means they can be stripped later.

    // The symbols that we might want to call as a user even if not
    // used in the Halide-generated code must remain weak. This is
    // handled automatically by assuming any symbol starting with
    // "halide_" that is weak will be retained. There are a few
    // symbols for which this convention is not followed and these are
    // in this array.
    vector<string> retain = {"__stack_chk_guard",
                             "__stack_chk_fail"};

    if (t.has_feature(Target::MinGW)) {
        retain.insert(retain.end(),
                             {"sincos", "sincosf",
                              "asinh", "asinhf",
                              "acosh", "acoshf",
                              "atanh", "atanhf"});
    }

    // Enumerate the global variables.
    for (auto &gv : modules[0]->globals()) {
        // No variables are part of the public interface (even the ones labelled halide_)
        llvm::GlobalValue::LinkageTypes linkage = gv.getLinkage();
        if (linkage == llvm::GlobalValue::WeakAnyLinkage) {
            gv.setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
        } else if (linkage == llvm::GlobalValue::WeakODRLinkage) {
            gv.setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
        }
    }

    // Enumerate the functions.
    for (auto &f : *modules[0]) {
        bool can_strip = true;
        for (const string &r : retain) {
            if (f.getName() == r) {
                can_strip = false;
            }
        }

        bool is_halide_extern_c_sym = Internal::starts_with(f.getName(), "halide_");
        internal_assert(!is_halide_extern_c_sym || f.isWeakForLinker() || f.isDeclaration())
            << " for function " << (std::string)f.getName() << "\n";
        can_strip = can_strip && !is_halide_extern_c_sym;

        llvm::GlobalValue::LinkageTypes linkage = f.getLinkage();
        if (can_strip) {
            if (linkage == llvm::GlobalValue::WeakAnyLinkage) {
                f.setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
            } else if (linkage == llvm::GlobalValue::WeakODRLinkage) {
                f.setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
            }
        }
    }

    // Now remove the force-usage global that prevented clang from
    // dropping functions from the initial module.
    llvm::GlobalValue *llvm_used = modules[0]->getNamedGlobal("llvm.used");
    if (llvm_used) {
        llvm_used->eraseFromParent();
    }

    // Also drop the dummy runtime api usage. We only needed it so
    // that the declarations are retained in the module during the
    // linking procedure above.
    llvm::GlobalValue *runtime_api =
        modules[0]->getNamedGlobal("halide_runtime_api_functions");
    if (runtime_api) {
        runtime_api->eraseFromParent();
    }

}

}  // namespace

namespace Internal {

/** When JIT-compiling on 32-bit windows, we need to rewrite calls
 *  to name-mangled win32 api calls to non-name-mangled versions.
 */
void undo_win32_name_mangling(llvm::Module *m) {
    llvm::IRBuilder<> builder(m->getContext());
    // For every function prototype...
    for (llvm::Module::iterator iter = m->begin(); iter != m->end(); ++iter) {
        llvm::Function &f = *iter;
        string n = f.getName();
        // if it's a __stdcall call that starts with \01_, then we're making a win32 api call
        if (f.getCallingConv() == llvm::CallingConv::X86_StdCall &&
            f.empty() &&
            n.size() > 2 && n[0] == 1 && n[1] == '_') {

            // Unmangle the name.
            string unmangled_name = n.substr(2);
            size_t at = unmangled_name.rfind('@');
            unmangled_name = unmangled_name.substr(0, at);

            // Extern declare the unmangled version.
            llvm::Function *unmangled = llvm::Function::Create(f.getFunctionType(), f.getLinkage(), unmangled_name, m);
            unmangled->setCallingConv(f.getCallingConv());

            // Add a body to the mangled version that calls the unmangled version.
            llvm::BasicBlock *block = llvm::BasicBlock::Create(m->getContext(), "entry", &f);
            builder.SetInsertPoint(block);

            vector<llvm::Value *> args;
            for (auto &arg : f.args()) {
                args.push_back(&arg);
            }

            llvm::CallInst *c = builder.CreateCall(unmangled, args);
            c->setCallingConv(f.getCallingConv());

            if (f.getReturnType()->isVoidTy()) {
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
    string posix_fns[] = {"vsnprintf", "open", "close", "write", "fileno"};

    string *posix_fns_begin = posix_fns;
    string *posix_fns_end = posix_fns + sizeof(posix_fns) / sizeof(posix_fns[0]);

    for (auto &fn : *m) {
        for (auto &basic_block : fn) {
            for (auto &instruction : basic_block) {
                if (llvm::CallInst *call = llvm::dyn_cast<llvm::CallInst>(&instruction)) {
                    if (llvm::Function *called_fn = call->getCalledFunction()) {
                        if (std::find(posix_fns_begin, posix_fns_end, called_fn->getName()) != posix_fns_end) {
                            add_underscore_to_posix_call(call, called_fn, m);
                        }
                    }
                }
            }
        }
    }
}

/** Create an llvm module containing the support code for a given target. */
std::unique_ptr<llvm::Module> get_initial_module_for_target(Target t, llvm::LLVMContext *c, bool for_shared_jit_runtime, bool just_gpu) {
    enum InitialModuleType {
        ModuleAOT,
        ModuleAOTNoRuntime,
        ModuleJITShared,
        ModuleJITInlined,
        ModuleGPU
    } module_type;

    if (t.has_feature(Target::JIT)) {
        if (just_gpu) {
            module_type = ModuleGPU;
        } else if (for_shared_jit_runtime) {
            module_type = ModuleJITShared;
        } else {
            module_type = ModuleJITInlined;
        }
    } else if (t.has_feature(Target::NoRuntime)) {
        module_type = ModuleAOTNoRuntime;
    } else {
        module_type = ModuleAOT;
    }

    //    Halide::Internal::debug(0) << "Getting initial module type " << (int)module_type << "\n";

    internal_assert(t.bits == 32 || t.bits == 64)
        << "Bad target: " << t.to_string();
    bool bits_64 = (t.bits == 64);
    bool debug = t.has_feature(Target::Debug);
    bool tsan = t.has_feature(Target::TSAN);

    vector<std::unique_ptr<llvm::Module>> modules;

    if (module_type != ModuleGPU) {
        if (module_type != ModuleJITInlined && module_type != ModuleAOTNoRuntime) {
            // OS-dependent modules
            if (t.os == Target::Linux) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                if (t.arch == Target::X86) {
                    modules.push_back(get_initmod_linux_clock(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_io(c, bits_64, debug));
                modules.push_back(get_initmod_posix_tempfile(c, bits_64, debug));
                modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_linux_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_get_symbol(c, bits_64, debug));
                //This is just to verify the hexagon_dma mock tests 
                modules.push_back(get_initmod_default_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::OSX) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_osx_clock(c, bits_64, debug));
                modules.push_back(get_initmod_posix_io(c, bits_64, debug));
                modules.push_back(get_initmod_posix_tempfile(c, bits_64, debug));
                modules.push_back(get_initmod_osx_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_osx_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_osx_get_symbol(c, bits_64, debug));
                modules.push_back(get_initmod_default_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::Android) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                if (t.arch == Target::ARM) {
                    modules.push_back(get_initmod_android_clock(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
                }
                modules.push_back(get_initmod_android_io(c, bits_64, debug));
                modules.push_back(get_initmod_android_tempfile(c, bits_64, debug));
                modules.push_back(get_initmod_android_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_linux_yield(c, bits_64, debug)); // TODO: verify
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_get_symbol(c, bits_64, debug));
                modules.push_back(get_initmod_default_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::Windows) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_windows_clock(c, bits_64, debug));
                modules.push_back(get_initmod_windows_io(c, bits_64, debug));
                modules.push_back(get_initmod_windows_tempfile(c, bits_64, debug));
                modules.push_back(get_initmod_windows_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_windows_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_windows_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_windows_get_symbol(c, bits_64, debug));
                if (t.has_feature(Target::MinGW)) {
                    modules.push_back(get_initmod_mingw_math(c, bits_64, debug));
                }
                modules.push_back(get_initmod_default_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::IOS) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
                modules.push_back(get_initmod_ios_io(c, bits_64, debug));
                modules.push_back(get_initmod_posix_tempfile(c, bits_64, debug));
                modules.push_back(get_initmod_osx_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_osx_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_default_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::QuRT) {
                modules.push_back(get_initmod_hexagon_cache_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_qurt_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_qurt_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_qurt_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_qurt_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_qurt_init_fini(c, bits_64, debug));
                modules.push_back(get_initmod_hexagon_cache_allocator(c, bits_64, debug));
            } else if (t.os == Target::NoOS) {
                // The OS-specific symbols provided by the modules
                // above are expected to be provided by the containing
                // process instead at link time. Less aggressive than
                // NoRuntime, as OS-agnostic modules like tracing are
                // still included below.
                if (t.arch == Target::Hexagon) {
                    modules.push_back(get_initmod_qurt_allocator(c, bits_64, debug));
                    modules.push_back(get_initmod_hexagon_cache_allocator(c, bits_64, debug));
                }
                modules.push_back(get_initmod_fake_thread_pool(c, bits_64, debug));
            }
        }

        if (module_type != ModuleJITShared) {
            // The first module for inline only case has to be C/C++ compiled otherwise the
            // datalayout is not properly setup.
            modules.push_back(get_initmod_buffer_t(c, bits_64, debug));
            modules.push_back(get_initmod_destructors(c, bits_64, debug));

            // Math intrinsics vary slightly across platforms
            if (t.os == Target::Windows) {
                if (t.bits == 32) {
                    modules.push_back(get_initmod_win32_math_ll(c));
                } else {
                    modules.push_back(get_initmod_posix_math_ll(c));
                }
            } else {
                modules.push_back(get_initmod_posix_math_ll(c));
            }
        }

        if (module_type != ModuleJITInlined && module_type != ModuleAOTNoRuntime) {
            // These modules are always used and shared
            modules.push_back(get_initmod_gpu_device_selection(c, bits_64, debug));
            if (t.arch != Target::Hexagon) {
                // These modules don't behave correctly on a real
                // Hexagon device (they do work in the simulator
                // though...).
                modules.push_back(get_initmod_tracing(c, bits_64, debug));
                modules.push_back(get_initmod_write_debug_image(c, bits_64, debug));

                // TODO: Support this module in the Hexagon backend,
                // currently generates assert at src/HexagonOffload.cpp:279
                modules.push_back(get_initmod_cache(c, bits_64, debug));
            }
            modules.push_back(get_initmod_to_string(c, bits_64, debug));

            if (t.arch == Target::Hexagon ||
                t.has_feature(Target::HVX_64) ||
                t.has_feature(Target::HVX_128)) {
                modules.push_back(get_initmod_alignment_128(c, bits_64, debug));
            } else {
                modules.push_back(get_initmod_alignment_32(c, bits_64, debug));
            }

            modules.push_back(get_initmod_device_interface(c, bits_64, debug));
            modules.push_back(get_initmod_metadata(c, bits_64, debug));
            modules.push_back(get_initmod_float16_t(c, bits_64, debug));
            modules.push_back(get_initmod_errors(c, bits_64, debug));


            // Note that we deliberately include this module, even if Target::LegacyBufferWrappers
            // isn't enabled: it isn't much code, and it makes it much easier to
            // intermingle code that is built with this flag with code that is
            // built without.
            modules.push_back(get_initmod_old_buffer_t(c, bits_64, debug));

            // MIPS doesn't support the atomics the profiler requires.
            if (t.arch != Target::MIPS && t.os != Target::NoOS &&
                t.os != Target::QuRT) {
                if (t.os == Target::Windows) {
                    modules.push_back(get_initmod_windows_profiler(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_profiler(c, bits_64, debug));
                }
            }

            if (t.has_feature(Target::MSAN)) {
                modules.push_back(get_initmod_msan(c, bits_64, debug));
            } else {
                modules.push_back(get_initmod_msan_stubs(c, bits_64, debug));
            }
        }

        if (module_type != ModuleJITShared) {
            // These modules are optional
            if (t.arch == Target::X86) {
                modules.push_back(get_initmod_x86_ll(c));
            }
            if (t.arch == Target::ARM) {
                if (t.bits == 64) {
                  modules.push_back(get_initmod_aarch64_ll(c));
                } else if (t.has_feature(Target::ARMv7s)) {
                    modules.push_back(get_initmod_arm_ll(c));
                } else if (!t.has_feature(Target::NoNEON)) {
                    modules.push_back(get_initmod_arm_ll(c));
                } else {
                    modules.push_back(get_initmod_arm_no_neon_ll(c));
                }
            }
            if (t.arch == Target::MIPS) {
                modules.push_back(get_initmod_mips_ll(c));
            }
            if (t.arch == Target::POWERPC) {
                modules.push_back(get_initmod_powerpc_ll(c));
            }
            if (t.arch == Target::Hexagon) {
                modules.push_back(get_initmod_qurt_hvx(c, bits_64, debug));
                if (t.has_feature(Target::HVX_64)) {
                    modules.push_back(get_initmod_hvx_64_ll(c));
                } else if (t.has_feature(Target::HVX_128)) {
                    modules.push_back(get_initmod_hvx_128_ll(c));
                }
            } else {
                modules.push_back(get_initmod_prefetch(c, bits_64, debug));
            }
            if (t.has_feature(Target::SSE41)) {
                modules.push_back(get_initmod_x86_sse41_ll(c));
            }
            if (t.has_feature(Target::AVX)) {
                modules.push_back(get_initmod_x86_avx_ll(c));
            }
            if (t.has_feature(Target::AVX2)) {
                modules.push_back(get_initmod_x86_avx2_ll(c));
            }
            if (t.has_feature(Target::Profile)) {
                modules.push_back(get_initmod_profiler_inlined(c, bits_64, debug));
            }
        }

        if (module_type == ModuleAOT) {
            // These modules are only used for AOT compilation
            modules.push_back(get_initmod_can_use_target(c, bits_64, debug));
            if (t.arch == Target::X86) {
                modules.push_back(get_initmod_x86_cpu_features(c, bits_64, debug));
            }
            if (t.arch == Target::ARM) {
                if (t.bits == 64) {
                    modules.push_back(get_initmod_aarch64_cpu_features(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_arm_cpu_features(c, bits_64, debug));
                }
            }
            if (t.arch == Target::MIPS) {
                modules.push_back(get_initmod_mips_cpu_features(c, bits_64, debug));
            }
            if (t.arch == Target::POWERPC) {
                modules.push_back(get_initmod_powerpc_cpu_features(c, bits_64, debug));
            }
            if (t.arch == Target::Hexagon) {
                modules.push_back(get_initmod_hexagon_cpu_features(c, bits_64, debug));
            }
        }

    }

    if (module_type == ModuleJITShared || module_type == ModuleGPU) {
        modules.push_back(get_initmod_module_jit_ref_count(c, bits_64, debug));
    } else if (module_type == ModuleAOT) {
        modules.push_back(get_initmod_module_aot_ref_count(c, bits_64, debug));
    }

    if (module_type == ModuleAOT || module_type == ModuleGPU) {
        if (t.has_feature(Target::CUDA)) {
            if (t.os == Target::Windows) {
                modules.push_back(get_initmod_windows_cuda(c, bits_64, debug));
            } else {
                modules.push_back(get_initmod_cuda(c, bits_64, debug));
            }
        }
        if (t.has_feature(Target::OpenCL)) {
            if (t.os == Target::Windows) {
                modules.push_back(get_initmod_windows_opencl(c, bits_64, debug));
            } else {
                modules.push_back(get_initmod_opencl(c, bits_64, debug));
            }
        }
        if (t.has_feature(Target::OpenGL)) {
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
        }
        if (t.has_feature(Target::OpenGLCompute)) {
            modules.push_back(get_initmod_openglcompute(c, bits_64, debug));
            if (t.os == Target::Android) {
                // Only platform that supports OpenGL Compute for now.
                modules.push_back(get_initmod_android_opengl_context(c, bits_64, debug));
            } else if (t.os == Target::Linux) {
                modules.push_back(get_initmod_linux_opengl_context(c, bits_64, debug));
            } else if (t.os == Target::OSX) {
                modules.push_back(get_initmod_osx_opengl_context(c, bits_64, debug));
            } else {
                // You're on your own to provide definitions of halide_opengl_get_proc_address and halide_opengl_create_context
            }

        }
        if (t.has_feature(Target::Metal)) {
            modules.push_back(get_initmod_metal(c, bits_64, debug));
            if (t.arch == Target::ARM) {
                modules.push_back(get_initmod_metal_objc_arm(c, bits_64, debug));
            } else if (t.arch == Target::X86) {
                modules.push_back(get_initmod_metal_objc_x86(c, bits_64, debug));
            } else {
                user_error << "Metal can only be used on ARM or X86 architectures.\n";
            }
        }
        if (t.arch != Target::Hexagon && t.features_any_of({Target::HVX_64, Target::HVX_128})) {
            modules.push_back(get_initmod_module_jit_ref_count(c, bits_64, debug));
            modules.push_back(get_initmod_hexagon_host(c, bits_64, debug));
        }
        if (t.has_feature(Target::HexagonDma)) {
            modules.push_back(get_initmod_hexagon_dma(c, bits_64, debug));
        }
    }

    if (module_type == ModuleAOT && t.has_feature(Target::Matlab)) {
        modules.push_back(get_initmod_matlab(c, bits_64, debug));
    }

    if (module_type == ModuleAOTNoRuntime ||
        module_type == ModuleJITInlined ||
        t.os == Target::NoOS) {
        modules.push_back(get_initmod_runtime_api(c, bits_64, debug));
    }

    link_modules(modules, t);

    if (t.os == Target::Windows &&
        t.bits == 32 &&
        (t.has_feature(Target::JIT))) {
        undo_win32_name_mangling(modules[0].get());
    }

    if (t.os == Target::Windows) {
        add_underscores_to_posix_calls_on_windows(modules[0].get());
    }

    return std::move(modules[0]);
}

#ifdef WITH_PTX
std::unique_ptr<llvm::Module> get_initial_module_for_ptx_device(Target target, llvm::LLVMContext *c) {
    std::vector<std::unique_ptr<llvm::Module>> modules;
    modules.push_back(get_initmod_ptx_dev_ll(c));

    std::unique_ptr<llvm::Module> module;

    // This table is based on the guidance at:
    // http://docs.nvidia.com/cuda/libdevice-users-guide/basic-usage.html#linking-with-libdevice
    if (target.has_feature(Target::CUDACapability35)) {
        module = get_initmod_ptx_compute_35_ll(c);
    } else if (target.features_any_of({Target::CUDACapability32,
                                       Target::CUDACapability50})) {
        // For some reason sm_32 and sm_50 use libdevice 20
        module = get_initmod_ptx_compute_20_ll(c);
    } else if (target.has_feature(Target::CUDACapability30)) {
        module = get_initmod_ptx_compute_30_ll(c);
    } else {
        module = get_initmod_ptx_compute_20_ll(c);
    }
    modules.push_back(std::move(module));

    link_modules(modules, target);

    // For now, the PTX backend does not handle calling functions. So mark all functions
    // AvailableExternally to ensure they are inlined or deleted.
    for (llvm::Module::iterator iter = modules[0]->begin(); iter != modules[0]->end(); iter++) {
        llvm::Function &f = *iter;

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

        if (!f.isDeclaration() && !f.hasFnAttribute(llvm::Attribute::NoInline)) {
            f.setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
        }

        // Also mark the halide_gpu_thread_barrier as noduplicate.
        if (f.getName() == "halide_gpu_thread_barrier") {
            f.addFnAttr(llvm::Attribute::NoDuplicate);
        }
    }

    llvm::Triple triple("nvptx64--");
    modules[0]->setTargetTriple(triple.str());

    llvm::DataLayout dl("e-i64:64-v16:16-v32:32-n16:32:64");
    modules[0]->setDataLayout(dl);

    return std::move(modules[0]);
}
#endif

void add_bitcode_to_module(llvm::LLVMContext *context, llvm::Module &module,
                           const std::vector<uint8_t> &bitcode, const std::string &name) {
    llvm::StringRef sb = llvm::StringRef((const char *)&bitcode[0], bitcode.size());
    std::unique_ptr<llvm::Module> add_in = parse_bitcode_file(sb, context, name.c_str());

    bool failed = llvm::Linker::linkModules(module, std::move(add_in));
    if (failed) {
        internal_error << "Failure linking in additional module: " << name << "\n";
    }
}

}  // namespace Internal

}  // namespace Halide

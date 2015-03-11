#include "LLVM_Runtime_Linker.h"

#include <memory>
#include "LLVM_Headers.h"

namespace Halide {

using std::string;
using std::vector;

using Internal::vec;

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
DECLARE_CPP_INITMOD(module_jit_ref_count)
DECLARE_CPP_INITMOD(module_aot_ref_count)
DECLARE_CPP_INITMOD(device_interface)

#ifdef WITH_ARM
DECLARE_LL_INITMOD(arm)
DECLARE_LL_INITMOD(arm_no_neon)
#else
DECLARE_NO_INITMOD(arm)
DECLARE_NO_INITMOD(arm_no_neon)
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
        #if LLVM_VERSION >= 37
        data_layout = &modules[i]->getDataLayout();
        #else
        data_layout = modules[i]->getDataLayout();
        #endif
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
        #if LLVM_VERSION >= 37
        modules[i]->setDataLayout(*data_layout);
        #elif LLVM_VERSION >= 35
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
    // must remain weak. This is handled automatically by assuming any
    // symbol starting with "halide_" that is weak will be retained. There
    // are a few compiler generated symbols for which this convention is not
    // followed and these are in this array.
    string retain[] = {"__stack_chk_guard",
                       "__stack_chk_fail",
                       ""};

    llvm::Module *module = modules[0];

    // Enumerate the global variables.
    for (llvm::Module::global_iterator iter = module->global_begin(); iter != module->global_end(); iter++) {
        if (llvm::GlobalValue *gv = llvm::dyn_cast<llvm::GlobalValue>(iter)) {
            if (Internal::starts_with(gv->getName(), "halide_")) {
                internal_assert(gv->hasExternalLinkage() || gv->isWeakForLinker() || gv->isDeclaration()) <<
                    " for global variable " << (std::string)gv->getName() << "\n";
                llvm::GlobalValue::LinkageTypes t = gv->getLinkage();
                if (t == llvm::GlobalValue::WeakAnyLinkage) {
                    gv->setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
                } else if (t == llvm::GlobalValue::WeakODRLinkage) {
                    gv->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
                }
            }
        }
    }

    // Enumerate the functions.
    for (llvm::Module::iterator iter = module->begin(); iter != module->end(); iter++) {
        llvm::Function *f = (llvm::Function *)(iter);

        bool can_strip = true;
        for (size_t i = 0; !retain[i].empty(); i++) {
            if (f->getName() == retain[i]) {
                can_strip = false;
            }
        }

        bool is_halide_extern_c_sym = Internal::starts_with(f->getName(), "halide_");
        internal_assert(!is_halide_extern_c_sym || f->isWeakForLinker() || f->isDeclaration()) <<
            " for function " << (std::string)f->getName() << "\n";
        can_strip = can_strip && !(is_halide_extern_c_sym && f->mayBeOverridden());

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
llvm::Module *get_initial_module_for_target(Target t, llvm::LLVMContext *c, bool for_shared_jit_runtime, bool just_gpu) {
    enum InitialModuleType {
        ModuleAOT,
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
    } else {
        module_type = ModuleAOT;
    }

    //    Halide::Internal::debug(0) << "Getting initial module type " << (int)module_type << "\n";

    internal_assert(t.bits == 32 || t.bits == 64);
    // NaCl always uses the 32-bit runtime modules, because pointers
    // and size_t are 32-bit in 64-bit NaCl, and that's the only way
    // in which the 32- and 64-bit runtimes differ.
    bool bits_64 = (t.bits == 64) && (t.os != Target::NaCl);
    bool debug = t.has_feature(Target::Debug);

    vector<llvm::Module *> modules;

    if (module_type != ModuleGPU) {
        if (module_type != ModuleJITInlined) {
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
        }

        if (module_type != ModuleJITShared) {
            // The first module for inline only case has to be C/C++ compiled otherwise the
            // datalayout is not properly setup.
            modules.push_back(get_initmod_posix_math(c, bits_64, debug));
            // Math intrinsics vary slightly across platforms
            if (t.os == Target::Windows && t.bits == 32) {
                modules.push_back(get_initmod_win32_math_ll(c));
            } else if (t.arch == Target::PNaCl) {
                modules.push_back(get_initmod_pnacl_math_ll(c));
            } else {
                modules.push_back(get_initmod_posix_math_ll(c));
            }
        }

        if (module_type != ModuleJITInlined) {
            // These modules are always used and shared
            modules.push_back(get_initmod_gpu_device_selection(c, bits_64, debug));
            modules.push_back(get_initmod_tracing(c, bits_64, debug));
            modules.push_back(get_initmod_write_debug_image(c, bits_64, debug));
            modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
            modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
            modules.push_back(get_initmod_posix_print(c, bits_64, debug));
            modules.push_back(get_initmod_cache(c, bits_64, debug));
            modules.push_back(get_initmod_to_string(c, bits_64, debug));
            modules.push_back(get_initmod_device_interface(c, bits_64, debug));
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
            if (t.has_feature(Target::SSE41)) {
                modules.push_back(get_initmod_x86_sse41_ll(c));
            }
            if (t.has_feature(Target::AVX)) {
                modules.push_back(get_initmod_x86_avx_ll(c));
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
        }
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

    if (t.arch == Target::PNaCl) {
        // The initial module is supposed to have the right datalayout
        // already, but this is not the case for PNaCl.
        modules[0]->setDataLayout("e-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-p:32:32:32-v128:32:32");
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

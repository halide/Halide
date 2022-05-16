#include "LLVM_Runtime_Linker.h"
#include "Error.h"
#include "LLVM_Headers.h"
#include "Target.h"

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

#define DECLARE_INITMOD(mod)                                                              \
    extern "C" unsigned char halide_internal_initmod_##mod[];                             \
    extern "C" int halide_internal_initmod_##mod##_length;                                \
    std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *context) {         \
        llvm::StringRef sb = llvm::StringRef((const char *)halide_internal_initmod_##mod, \
                                             halide_internal_initmod_##mod##_length);     \
        return parse_bitcode_file(sb, context, #mod);                                     \
    }

#define DECLARE_NO_INITMOD(mod)                                                                        \
    std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *, bool = false, bool = false) { \
        user_error << "Halide was compiled without support for this target\n";                         \
        return std::unique_ptr<llvm::Module>();                                                        \
    }                                                                                                  \
    std::unique_ptr<llvm::Module> get_initmod_##mod##_ll(llvm::LLVMContext *) {                        \
        user_error << "Halide was compiled without support for this target\n";                         \
        return std::unique_ptr<llvm::Module>();                                                        \
    }

#define DECLARE_CPP_INITMOD_LOOKUP(mod)                                                                     \
    std::unique_ptr<llvm::Module> get_initmod_##mod(llvm::LLVMContext *context, bool bits_64, bool debug) { \
        if (bits_64) {                                                                                      \
            if (debug) {                                                                                    \
                return get_initmod_##mod##_64_debug(context);                                               \
            } else {                                                                                        \
                return get_initmod_##mod##_64(context);                                                     \
            }                                                                                               \
        } else {                                                                                            \
            if (debug) {                                                                                    \
                return get_initmod_##mod##_32_debug(context);                                               \
            } else {                                                                                        \
                return get_initmod_##mod##_32(context);                                                     \
            }                                                                                               \
        }                                                                                                   \
    }

#define DECLARE_CPP_INITMOD(mod)    \
    DECLARE_INITMOD(mod##_32_debug) \
    DECLARE_INITMOD(mod##_64_debug) \
    DECLARE_INITMOD(mod##_32)       \
    DECLARE_INITMOD(mod##_64)       \
    DECLARE_CPP_INITMOD_LOOKUP(mod)

#define DECLARE_LL_INITMOD(mod) \
    DECLARE_INITMOD(mod##_ll)

// Universal CPP Initmods. Please keep sorted alphabetically.
DECLARE_CPP_INITMOD(alignment_128)
DECLARE_CPP_INITMOD(alignment_32)
DECLARE_CPP_INITMOD(allocation_cache)
DECLARE_CPP_INITMOD(alignment_64)
DECLARE_CPP_INITMOD(android_clock)
DECLARE_CPP_INITMOD(android_host_cpu_count)
DECLARE_CPP_INITMOD(android_io)
DECLARE_CPP_INITMOD(halide_buffer_t)
DECLARE_CPP_INITMOD(cache)
DECLARE_CPP_INITMOD(can_use_target)
DECLARE_CPP_INITMOD(cuda)
DECLARE_CPP_INITMOD(destructors)
DECLARE_CPP_INITMOD(device_interface)
DECLARE_CPP_INITMOD(errors)
DECLARE_CPP_INITMOD(fake_get_symbol)
DECLARE_CPP_INITMOD(fake_thread_pool)
DECLARE_CPP_INITMOD(float16_t)
DECLARE_CPP_INITMOD(force_include_types)
DECLARE_CPP_INITMOD(fuchsia_clock)
DECLARE_CPP_INITMOD(fuchsia_host_cpu_count)
DECLARE_CPP_INITMOD(fuchsia_yield)
DECLARE_CPP_INITMOD(gpu_device_selection)
DECLARE_CPP_INITMOD(hexagon_dma)
DECLARE_CPP_INITMOD(hexagon_host)
DECLARE_CPP_INITMOD(ios_io)
DECLARE_CPP_INITMOD(linux_clock)
DECLARE_CPP_INITMOD(linux_host_cpu_count)
DECLARE_CPP_INITMOD(linux_yield)
DECLARE_CPP_INITMOD(module_aot_ref_count)
DECLARE_CPP_INITMOD(module_jit_ref_count)
DECLARE_CPP_INITMOD(msan)
DECLARE_CPP_INITMOD(msan_stubs)
DECLARE_CPP_INITMOD(opencl)
DECLARE_CPP_INITMOD(openglcompute)
DECLARE_CPP_INITMOD(opengl_egl_context)
DECLARE_CPP_INITMOD(opengl_glx_context)
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
DECLARE_CPP_INITMOD(posix_print)
DECLARE_CPP_INITMOD(posix_threads)
DECLARE_CPP_INITMOD(posix_threads_tsan)
DECLARE_CPP_INITMOD(prefetch)
DECLARE_CPP_INITMOD(profiler)
DECLARE_CPP_INITMOD(timer_profiler)
DECLARE_CPP_INITMOD(posix_timer_profiler)
DECLARE_CPP_INITMOD(profiler_inlined)
DECLARE_CPP_INITMOD(pseudostack)
DECLARE_CPP_INITMOD(qurt_allocator)
DECLARE_CPP_INITMOD(hexagon_cache_allocator)
DECLARE_CPP_INITMOD(hexagon_dma_pool)
DECLARE_CPP_INITMOD(qurt_hvx)
DECLARE_CPP_INITMOD(qurt_hvx_vtcm)
DECLARE_CPP_INITMOD(qurt_threads)
DECLARE_CPP_INITMOD(qurt_threads_tsan)
DECLARE_CPP_INITMOD(qurt_yield)
DECLARE_CPP_INITMOD(runtime_api)
DECLARE_CPP_INITMOD(to_string)
DECLARE_CPP_INITMOD(trace_helper)
DECLARE_CPP_INITMOD(tracing)
DECLARE_CPP_INITMOD(windows_clock)
DECLARE_CPP_INITMOD(windows_cuda)
DECLARE_CPP_INITMOD(windows_get_symbol)
DECLARE_CPP_INITMOD(windows_io)
DECLARE_CPP_INITMOD(windows_opencl)
DECLARE_CPP_INITMOD(windows_profiler)
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
#ifdef WITH_AARCH64
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

#ifdef WITH_NVPTX
DECLARE_LL_INITMOD(ptx_compute_20)
DECLARE_LL_INITMOD(ptx_compute_30)
DECLARE_LL_INITMOD(ptx_compute_35)
#endif  // WITH_NVPTX

#if defined(WITH_D3D12) && defined(WITH_X86)
DECLARE_CPP_INITMOD(windows_d3d12compute_x86)
#else
DECLARE_NO_INITMOD(windows_d3d12compute_x86)
#endif

#ifdef WITH_D3D12
#ifdef WITH_ARM
DECLARE_INITMOD(windows_d3d12compute_arm_32)
DECLARE_INITMOD(windows_d3d12compute_arm_32_debug)
#else
DECLARE_NO_INITMOD(windows_d3d12compute_arm_32)
DECLARE_NO_INITMOD(windows_d3d12compute_arm_32_debug)
#endif

#ifdef WITH_AARCH64
DECLARE_INITMOD(windows_d3d12compute_arm_64)
DECLARE_INITMOD(windows_d3d12compute_arm_64_debug)
#else
DECLARE_NO_INITMOD(windows_d3d12compute_arm_64)
DECLARE_NO_INITMOD(windows_d3d12compute_arm_64_debug)
#endif

DECLARE_CPP_INITMOD_LOOKUP(windows_d3d12compute_arm)
#else
DECLARE_NO_INITMOD(windows_d3d12compute_arm)
#endif  // WITH_D3D12

#ifdef WITH_X86
DECLARE_LL_INITMOD(x86_amx)
DECLARE_LL_INITMOD(x86_avx512)
DECLARE_LL_INITMOD(x86_avx2)
DECLARE_LL_INITMOD(x86_avx)
DECLARE_LL_INITMOD(x86)
DECLARE_LL_INITMOD(x86_sse41)
DECLARE_CPP_INITMOD(x86_cpu_features)
#else
DECLARE_NO_INITMOD(x86_amx)
DECLARE_NO_INITMOD(x86_avx512)
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
DECLARE_LL_INITMOD(hvx_128)
DECLARE_CPP_INITMOD(hexagon_cpu_features)
#else
DECLARE_NO_INITMOD(hvx_128)
DECLARE_NO_INITMOD(hexagon_cpu_features)
#endif  // WITH_HEXAGON

#ifdef WITH_WEBASSEMBLY
DECLARE_CPP_INITMOD(wasm_cpu_features)
DECLARE_LL_INITMOD(wasm_math)
#else
DECLARE_NO_INITMOD(wasm_cpu_features)
DECLARE_NO_INITMOD(wasm_math)
#endif  // WITH_WEBASSEMBLY

#ifdef WITH_RISCV
// DECLARE_LL_INITMOD(riscv)
DECLARE_CPP_INITMOD(riscv_cpu_features)
#else
// DECLARE_NO_INITMOD(riscv)
DECLARE_NO_INITMOD(riscv_cpu_features)
#endif  // WITH_RISCV

llvm::DataLayout get_data_layout_for_target(Target target) {
    if (target.arch == Target::X86) {
        if (target.bits == 32) {
            if (target.os == Target::OSX) {
                return llvm::DataLayout("e-m:o-p:32:32-p270:32:32-p271:32:32-p272:64:64-f64:32:64-f80:128-n8:16:32-S128");
            } else if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-p:32:32-p270:32:32-p271:32:32-p272:64:64-f64:32:64-f80:128-n8:16:32-S128");
            } else if (target.os == Target::Windows) {
#if LLVM_VERSION >= 140
                // For 32-bit MSVC targets, alignment of f80 values is 16 bytes (see https://reviews.llvm.org/D115942)
                if (!target.has_feature(Target::JIT)) {
                    return llvm::DataLayout("e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32-a:0:32-S32");
                } else {
                    return llvm::DataLayout("e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32-a:0:32-S32");
                }
#else
                if (!target.has_feature(Target::JIT)) {
                    return llvm::DataLayout("e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:32-n8:16:32-a:0:32-S32");
                } else {
                    return llvm::DataLayout("e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:32-n8:16:32-a:0:32-S32");
                }
#endif
            } else {
                // Linux/Android
                return llvm::DataLayout("e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-f64:32:64-f80:32-n8:16:32-S128");
            }
        } else {  // 64-bit
            if (target.os == Target::OSX) {
                return llvm::DataLayout("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::Windows && !target.has_feature(Target::JIT)) {
                return llvm::DataLayout("e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
            } else if (target.os == Target::Windows) {
                return llvm::DataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
            } else {
                return llvm::DataLayout("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
            }
        }
    } else if (target.arch == Target::ARM) {
        if (target.bits == 32) {
            if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-p:32:32-Fi8-f64:32:64-v64:32:64-v128:32:128-a:0:32-n32-S32");
            } else {
                return llvm::DataLayout("e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64");
            }
        } else {  // 64-bit
            if (target.os == Target::IOS) {
                return llvm::DataLayout("e-m:o-i64:64-i128:128-n32:64-S128");
            } else if (target.os == Target::OSX) {
                return llvm::DataLayout("e-m:o-i64:64-i128:128-n32:64-S128");
            } else if (target.os == Target::Windows) {
                return llvm::DataLayout("e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128");
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
    } else if (target.arch == Target::WebAssembly) {
#if LLVM_VERSION >= 140
        if (target.bits == 32) {
            return llvm::DataLayout("e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
        } else {
            return llvm::DataLayout("e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-n32:64-S128-ni:1:10:20");
        }
#else
        if (target.bits == 32) {
            return llvm::DataLayout("e-m:e-p:32:32-i64:64-n32:64-S128");
        } else {
            return llvm::DataLayout("e-m:e-p:64:64-i64:64-n32:64-S128");
        }
#endif
    } else if (target.arch == Target::RISCV) {
        // TODO: Valdidate this data layout is correct for RISCV. Assumption is it is like MIPS.
        if (target.bits == 32) {
            return llvm::DataLayout("e-m:e-p:32:32-i64:64-n32-S128");
        } else {
            return llvm::DataLayout("e-m:e-p:64:64-i64:64-i128:128-n64-S128");
        }
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
            triple.setEnvironment(llvm::Triple::MSVC);
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
        } else if (target.os == Target::Fuchsia) {
            triple.setOS(llvm::Triple::Fuchsia);
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
#ifdef WITH_AARCH64
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
        } else if (target.os == Target::Windows) {
            user_assert(target.bits == 64) << "Windows ARM targets must be 64-bit.\n";
            triple.setVendor(llvm::Triple::PC);
            triple.setOS(llvm::Triple::Win32);
            triple.setEnvironment(llvm::Triple::MSVC);
            if (target.has_feature(Target::JIT)) {
                // TODO(shoaibkamil): figure out a way to test this.
                // Currently blocked by https://github.com/halide/Halide/issues/5040
                user_error << "No JIT support for this OS/CPU combination yet.\n";
            }
        } else if (target.os == Target::Fuchsia) {
            triple.setOS(llvm::Triple::Fuchsia);
        } else if (target.os == Target::OSX) {
            triple.setVendor(llvm::Triple::Apple);
            triple.setOS(llvm::Triple::MacOSX);
            triple.setArchName("arm64");
        } else if (target.os == Target::NoOS) {
            // For bare-metal environments

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
#ifdef WITH_POWERPC
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
    } else if (target.arch == Target::WebAssembly) {
        triple.setVendor(llvm::Triple::UnknownVendor);
        if (target.bits == 32) {
            triple.setArch(llvm::Triple::wasm32);
        } else {
            triple.setArch(llvm::Triple::wasm64);
        }
        triple.setObjectFormat(llvm::Triple::Wasm);
    } else if (target.arch == Target::RISCV) {
        if (target.bits == 32) {
            triple.setArch(llvm::Triple::riscv32);
        } else {
            user_assert(target.bits == 64) << "Target must be 32- or 64-bit.\n";
            triple.setArch(llvm::Triple::riscv64);
        }

        if (target.os == Target::Linux) {
            triple.setOS(llvm::Triple::Linux);
        } else if (target.os == Target::NoOS) {
            // for baremetal environment
        } else {
            user_error << "No RISCV support for this OS\n";
        }
    } else {
        internal_error << "Bad target arch: " << target.arch << "\n";
    }

    return triple;
}

}  // namespace Internal

namespace {

void convert_weak_to_linkonce(llvm::GlobalValue &gv) {
    llvm::GlobalValue::LinkageTypes linkage = gv.getLinkage();
    if (linkage == llvm::GlobalValue::WeakAnyLinkage) {
        gv.setLinkage(llvm::GlobalValue::LinkOnceAnyLinkage);
    } else if (linkage == llvm::GlobalValue::WeakODRLinkage) {
        gv.setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
    } else if (linkage == llvm::GlobalValue::ExternalWeakLinkage) {
        gv.setLinkage(llvm::GlobalValue::ExternalLinkage);
    }
}

// Link all modules together and with the result in modules[0], all
// other input modules are destroyed. Sets the datalayout and target
// triple appropriately for the target.
void link_modules(std::vector<std::unique_ptr<llvm::Module>> &modules, Target t,
                  bool allow_stripping_all_weak_functions = false) {
    llvm::DataLayout data_layout = get_data_layout_for_target(t);
    llvm::Triple triple = Internal::get_triple_for_target(t);

    // Set the layout and triple on the modules before linking, so
    // llvm doesn't complain while combining them.
    for (auto &module : modules) {
        if (t.os == Target::Windows &&
            !Internal::starts_with(module->getName().str(), "windows_")) {
            // When compiling for windows, all wchars are
            // 16-bit. Generic modules may have it set to 32-bit. Drop
            // any module flags on the generic modules and use the
            // more correct ones on the windows-specific modules to
            // avoid a conflict. This is safe as long as the generic
            // modules never actually use a wchar.
            if (auto *module_flags = module->getModuleFlagsMetadata()) {
                module->eraseNamedMetadata(module_flags);
            }
        }
        module->setDataLayout(data_layout);
        module->setTargetTriple(triple.str());
    }

    // Link them all together
    for (size_t i = 1; i < modules.size(); i++) {
        bool failed = llvm::Linker::linkModules(*modules[0],
                                                std::move(modules[i]));
        if (failed) {
            internal_error << "Failure linking initial modules\n";
        }
    }

    // Now re-mark most weak symbols as linkonce. They are only weak to
    // prevent llvm from stripping them during initial module
    // assembly. This means they can be stripped later.

    // The symbols that we might want to call as a user even if not
    // used in the Halide-generated code must remain weak. This is
    // handled automatically by assuming any symbol starting with
    // "halide_" that is weak will be retained.

    // COMDAT is not supported in MachO object files, hence it does
    // not work on Mac OS or iOS. These sometimes show up in the
    // runtime since we compile for an abstract target that is based
    // on ELF. This code removes all Comdat items and leaves the
    // symbols they were attached to as regular definitions, which
    // only works if there is a single instance, which is generally
    // the case for the runtime. Presumably if this isn't true,
    // linking the module will fail.
    //
    // Comdats are left in for other platforms as they are required
    // for certain things on Windows and they are useful in general in
    // ELF based formats.
    if (t.os == Target::IOS || t.os == Target::OSX) {
        for (auto &global_obj : modules[0]->global_objects()) {
            global_obj.setComdat(nullptr);
        }
        modules[0]->getComdatSymbolTable().clear();
    }

    // Enumerate the global variables.
    for (auto &gv : modules[0]->globals()) {
        // No variables are part of the public interface (even the ones labelled halide_)
        convert_weak_to_linkonce(gv);
    }

    // Enumerate the functions.
    for (auto &f : *modules[0]) {
        const std::string f_name = Internal::get_llvm_function_name(f);
        assert(f_name != "__stack_chk_guard" && f_name != "__stack_chk_fail");

        bool is_halide_extern_c_sym = Internal::starts_with(f_name, "halide_");
        internal_assert(!is_halide_extern_c_sym || f.isWeakForLinker() || f.isDeclaration())
            << " for function " << f_name << "\n";

        // We never want *any* Function marked as external-weak here;
        // convert all of those to plain external.
        if (f.getLinkage() == llvm::GlobalValue::ExternalWeakLinkage) {
            f.setLinkage(llvm::GlobalValue::ExternalLinkage);
        } else {
            const bool can_strip = !is_halide_extern_c_sym;
            if (can_strip || allow_stripping_all_weak_functions) {
                convert_weak_to_linkonce(f);
            }
        }

        // Windows requires every symbol that's going to get merged
        // has a comdat that specifies how. The linkage type alone
        // isn't enough.
        if (t.os == Target::Windows && f.isWeakForLinker()) {
            llvm::Comdat *comdat = modules[0]->getOrInsertComdat(f_name);
            comdat->setSelectionKind(llvm::Comdat::Any);
            f.setComdat(comdat);
        }
    }

    // Now remove the force-usage global that prevented clang from
    // dropping functions from the initial module.
    llvm::GlobalValue *llvm_used = modules[0]->getNamedGlobal("llvm.used");
    if (llvm_used) {
        llvm_used->eraseFromParent();
    }

    llvm::GlobalValue *llvm_compiler_used =
        modules[0]->getNamedGlobal("llvm.compiler.used");
    if (llvm_compiler_used) {
        llvm_compiler_used->eraseFromParent();
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
        string n = get_llvm_function_name(f);
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

std::unique_ptr<llvm::Module> link_with_wasm_jit_runtime(llvm::LLVMContext *c, const Target &t,
                                                         std::unique_ptr<llvm::Module> extra_module) {
    bool bits_64 = (t.bits == 64);
    bool debug = t.has_feature(Target::Debug);

    // We only need to include things that must be linked in as callable entrypoints;
    // things that are 'alwaysinline' can be included here but are unnecessary.
    vector<std::unique_ptr<llvm::Module>> modules;
    modules.push_back(std::move(extra_module));
    modules.push_back(get_initmod_fake_thread_pool(c, bits_64, debug));
    modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
    modules.push_back(get_initmod_halide_buffer_t(c, bits_64, debug));
    modules.push_back(get_initmod_destructors(c, bits_64, debug));
    // These two aren't necessary, since they are 100% alwaysinline
    // modules.push_back(get_initmod_posix_math_ll(c));
    // modules.push_back(get_initmod_wasm_math_ll(c));
    modules.push_back(get_initmod_tracing(c, bits_64, debug));
    modules.push_back(get_initmod_cache(c, bits_64, debug));
    modules.push_back(get_initmod_to_string(c, bits_64, debug));
    modules.push_back(get_initmod_alignment_32(c, bits_64, debug));
    modules.push_back(get_initmod_device_interface(c, bits_64, debug));
    modules.push_back(get_initmod_force_include_types(c, bits_64, debug));
    modules.push_back(get_initmod_float16_t(c, bits_64, debug));
    modules.push_back(get_initmod_errors(c, bits_64, debug));
    modules.push_back(get_initmod_msan_stubs(c, bits_64, debug));

    // We don't want anything marked as weak for the wasm-jit runtime,
    // so convert all of them to linkonce
    constexpr bool allow_stripping_all_weak_functions = true;
    link_modules(modules, t, allow_stripping_all_weak_functions);

    return std::move(modules[0]);
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
                modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_linux_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_get_symbol(c, bits_64, debug));
            } else if (t.os == Target::WebAssemblyRuntime) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
                modules.push_back(get_initmod_posix_io(c, bits_64, debug));
                modules.push_back(get_initmod_linux_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_linux_yield(c, bits_64, debug));
                if (t.has_feature(Target::WasmThreads)) {
                    // Assume that the wasm libc will be providing pthreads
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_fake_thread_pool(c, bits_64, debug));
                }
                modules.push_back(get_initmod_fake_get_symbol(c, bits_64, debug));
            } else if (t.os == Target::OSX) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_osx_clock(c, bits_64, debug));
                modules.push_back(get_initmod_posix_io(c, bits_64, debug));
                modules.push_back(get_initmod_osx_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_osx_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_osx_get_symbol(c, bits_64, debug));
                modules.push_back(get_initmod_osx_host_cpu_count(c, bits_64, debug));
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
                modules.push_back(get_initmod_android_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_linux_yield(c, bits_64, debug));  // TODO: verify
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_get_symbol(c, bits_64, debug));
            } else if (t.os == Target::Windows) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_windows_clock(c, bits_64, debug));
                modules.push_back(get_initmod_windows_io(c, bits_64, debug));
                modules.push_back(get_initmod_windows_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_windows_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_windows_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_windows_get_symbol(c, bits_64, debug));
            } else if (t.os == Target::IOS) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_posix_clock(c, bits_64, debug));
                modules.push_back(get_initmod_ios_io(c, bits_64, debug));
                modules.push_back(get_initmod_osx_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_osx_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
            } else if (t.os == Target::QuRT) {
                modules.push_back(get_initmod_qurt_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_qurt_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_qurt_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_qurt_threads(c, bits_64, debug));
                }
            } else if (t.os == Target::NoOS) {
                // The OS-specific symbols provided by the modules
                // above are expected to be provided by the containing
                // process instead at link time. Less aggressive than
                // NoRuntime, as OS-agnostic modules like tracing are
                // still included below.
                if (t.arch == Target::Hexagon) {
                    modules.push_back(get_initmod_qurt_allocator(c, bits_64, debug));
                }
                modules.push_back(get_initmod_fake_thread_pool(c, bits_64, debug));
            } else if (t.os == Target::Fuchsia) {
                modules.push_back(get_initmod_posix_allocator(c, bits_64, debug));
                modules.push_back(get_initmod_posix_error_handler(c, bits_64, debug));
                modules.push_back(get_initmod_posix_print(c, bits_64, debug));
                modules.push_back(get_initmod_fuchsia_clock(c, bits_64, debug));
                modules.push_back(get_initmod_posix_io(c, bits_64, debug));
                modules.push_back(get_initmod_fuchsia_host_cpu_count(c, bits_64, debug));
                modules.push_back(get_initmod_fuchsia_yield(c, bits_64, debug));
                if (tsan) {
                    modules.push_back(get_initmod_posix_threads_tsan(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_posix_threads(c, bits_64, debug));
                }
                modules.push_back(get_initmod_posix_get_symbol(c, bits_64, debug));
            }
        }

        if (module_type != ModuleJITShared) {
            // The first module for inline only case has to be C/C++ compiled otherwise the
            // datalayout is not properly setup.
            modules.push_back(get_initmod_halide_buffer_t(c, bits_64, debug));
            modules.push_back(get_initmod_destructors(c, bits_64, debug));
            modules.push_back(get_initmod_pseudostack(c, bits_64, debug));
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
                modules.push_back(get_initmod_trace_helper(c, bits_64, debug));
                modules.push_back(get_initmod_write_debug_image(c, bits_64, debug));

                // TODO: Support this module in the Hexagon backend,
                // currently generates assert at src/HexagonOffload.cpp:279
                modules.push_back(get_initmod_cache(c, bits_64, debug));
            }
            modules.push_back(get_initmod_to_string(c, bits_64, debug));

            if (t.arch == Target::Hexagon ||
                t.has_feature(Target::HVX)) {
                modules.push_back(get_initmod_alignment_128(c, bits_64, debug));
            } else if (t.arch == Target::X86) {
                // AVX-512 requires 64-byte alignment. Could only increase alignment
                // if AVX-512 is in the target, but that falls afoul of linking
                // multiple versions of a filter for different levels of x86 -- weak
                // linking will pick one of the alignment modules unpredictably.
                // Another way to go is to query the CPU features and align by
                // 64 oonly if the procesor has AVX-512.
                // The choice to go 64 all the time is for simplicity and on the idea
                // that it won't be a noticeable cost in the majority of x86 usage.
                modules.push_back(get_initmod_alignment_64(c, bits_64, debug));
            } else {
                modules.push_back(get_initmod_alignment_32(c, bits_64, debug));
            }

            modules.push_back(get_initmod_allocation_cache(c, bits_64, debug));
            modules.push_back(get_initmod_device_interface(c, bits_64, debug));
            modules.push_back(get_initmod_float16_t(c, bits_64, debug));
            modules.push_back(get_initmod_errors(c, bits_64, debug));

            // Some environments don't support the atomics the profiler requires.
            if (t.arch != Target::MIPS && t.os != Target::NoOS && t.os != Target::QuRT) {
                if (t.has_feature(Target::ProfileByTimer)) {
                    user_assert(!t.has_feature(Target::Profile)) << "Can only use one of Target::Profile and Target::ProfileByTimer.";
                    // TODO(zvookin): This should work on all Posix like systems, but needs to be tested.
                    user_assert(t.os == Target::Linux) << "The timer based profiler currently can only be used on Linux.";
                    modules.push_back(get_initmod_profiler_inlined(c, bits_64, debug));
                    modules.push_back(get_initmod_timer_profiler(c, bits_64, debug));
                    modules.push_back(get_initmod_posix_timer_profiler(c, bits_64, debug));
                } else {
                    if (t.os == Target::Windows) {
                        modules.push_back(get_initmod_windows_profiler(c, bits_64, debug));
                    } else {
                        modules.push_back(get_initmod_profiler(c, bits_64, debug));
                    }
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
                modules.push_back(get_initmod_hvx_128_ll(c));
                if (t.features_any_of({Target::HVX_v65, Target::HVX_v66})) {
                    modules.push_back(get_initmod_qurt_hvx_vtcm(c, bits_64,
                                                                debug));
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
            if (t.has_feature(Target::AVX512)) {
                modules.push_back(get_initmod_x86_avx512_ll(c));
            }
            if (t.has_feature(Target::AVX512_SapphireRapids)) {
                modules.push_back(get_initmod_x86_amx_ll(c));
            }
            if (t.has_feature(Target::Profile)) {
                if (t.os == Target::WebAssemblyRuntime) {
                    user_assert(t.has_feature(Target::WasmThreads))
                        << "The profiler requires threads to operate; enable wasm_threads to use this under WebAssembly.";
                }
                modules.push_back(get_initmod_profiler_inlined(c, bits_64, debug));
            }
            if (t.has_feature(Target::ProfileByTimer)) {
                user_assert(!t.has_feature(Target::Profile)) << "Can only use one of Target::Profile and Target::ProfileByTimer.";
                // TODO(zvookin): This should work on all Posix like systems, but needs to be tested.
                user_assert(t.os == Target::Linux) << "The timer based profiler currently can only be used on Linux.";
                modules.push_back(get_initmod_profiler_inlined(c, bits_64, debug));
            }
            if (t.arch == Target::WebAssembly) {
                modules.push_back(get_initmod_wasm_math_ll(c));
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
            if (t.arch == Target::RISCV) {
                modules.push_back(get_initmod_riscv_cpu_features(c, bits_64, debug));
            }
            if (t.arch == Target::WebAssembly) {
                modules.push_back(get_initmod_wasm_cpu_features(c, bits_64, debug));
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
        if (t.has_feature(Target::OpenGLCompute)) {
            modules.push_back(get_initmod_openglcompute(c, bits_64, debug));
            if (t.os == Target::Android) {
                // Only platform that supports OpenGL Compute for now.
                modules.push_back(get_initmod_opengl_egl_context(c, bits_64, debug));
            } else if (t.os == Target::Linux) {
                if (t.has_feature(Target::EGL)) {
                    modules.push_back(get_initmod_opengl_egl_context(c, bits_64, debug));
                } else {
                    modules.push_back(get_initmod_opengl_glx_context(c, bits_64, debug));
                }
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
        if (t.has_feature(Target::D3D12Compute)) {
            user_assert(bits_64) << "D3D12Compute target only available on 64-bit targets for now.\n";
            user_assert(t.os == Target::Windows) << "D3D12Compute target only available on Windows targets.\n";
            if (t.arch == Target::X86) {
                modules.push_back(get_initmod_windows_d3d12compute_x86(c, bits_64, debug));
            } else if (t.arch == Target::ARM) {
                modules.push_back(get_initmod_windows_d3d12compute_arm(c, bits_64, debug));
            } else {
                user_error << "Direct3D 12 can only be used on ARM or X86 architectures.\n";
            }
        }
        if (t.arch != Target::Hexagon && t.has_feature(Target::HVX)) {
            modules.push_back(get_initmod_module_jit_ref_count(c, bits_64, debug));
            modules.push_back(get_initmod_hexagon_host(c, bits_64, debug));
        }
        if (t.has_feature(Target::HexagonDma)) {
            modules.push_back(get_initmod_hexagon_cache_allocator(c, bits_64, debug));
            modules.push_back(get_initmod_hexagon_dma(c, bits_64, debug));
            modules.push_back(get_initmod_hexagon_dma_pool(c, bits_64, debug));
        }
    }

    if (module_type == ModuleAOTNoRuntime ||
        module_type == ModuleJITInlined ||
        t.os == Target::NoOS) {
        modules.push_back(get_initmod_runtime_api(c, bits_64, debug));
    }

    modules.push_back(get_initmod_force_include_types(c, bits_64, debug));

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

#ifdef WITH_NVPTX
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
    for (auto &f : *modules[0]) {
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

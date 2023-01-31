#include "CodeGen_Xtensa.h"

#include <string>

#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Lerp.h"
#include "Simplify.h"
#include "Substitute.h"
#include "XtensaOptimize.h"

// 0 = off
// 1 == outermost loops only
// 2 == 2 outermost loop levels only
// etc
#define POOR_MANS_PROFILING_LOOP_LEVEL 0

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

std::string intrinsic_suffix_for_type(Type t) {
    if (t.is_int() && (t.bits() == 8)) {
        return "2NX8";
    } else if (t.is_uint() && (t.bits() == 8)) {
        return "2NX8U";
    } else if (t.is_int() && (t.bits() == 16)) {
        return "NX16";
    } else if (t.is_uint() && (t.bits() == 16)) {
        return "NX16U";
    } else if (t.is_int() && (t.bits() == 32)) {
        return "N_2X32";
    } else if (t.is_uint() && (t.bits() == 32)) {
        return "N_2X32U";
    } else if (t.is_float() && (t.bits() == 32)) {
        return "N_2XF32";
    } else if (t.is_float() && (t.bits() == 16)) {
        return "NXF16";
    }

    return "";
}

class UsesDmaCopy : public IRGraphVisitor {
private:
    using IRGraphVisitor::visit;

protected:
    void visit(const Call *op) override {
        if ((op->name == "halide_xtensa_copy_1d") || (op->name == "halide_xtensa_copy_2d")) {
            uses_dma = true;
            max_channel_no = std::max<int>(max_channel_no, *as_const_int(op->args[0]));
        }

        IRGraphVisitor::visit(op);
    }

public:
    bool uses_dma = false;
    int max_channel_no = 0;
};

void CodeGen_Xtensa::add_platform_prologue() {
    const char *headers = R"INLINE_CODE(

#define XCHAL_VISION_SIMD8 (XCHAL_VISION_SIMD16 * 2)

// TODO(vksnk): this is disabled by default, because iDMA is not part of cstub
// so we need to get git repo compiling with xt-tools first (b/173159625)

#ifdef __cplusplus
extern "C" {
#endif

extern void *halide_tcm_malloc(void *user_context, size_t x);
extern void halide_tcm_free(void *user_context, void *ptr);
extern void **halide_init_dma(int32_t channel_count);
extern int32_t halide_xtensa_copy_1d(int32_t channel, void* dst, int32_t dst_base, void* src, int32_t src_base, int32_t extent, int32_t item_size);
extern int32_t halide_xtensa_copy_2d(int32_t channel, void *dst, int32_t dst_base, int32_t dst_stride, void *src, int32_t src_base, int32_t src_stride, int32_t extent0, int32_t extent1, int32_t item_size);
extern int32_t halide_xtensa_wait_for_copy(int32_t channel);
extern int32_t halide_release_dma(int32_t channel_count, void** dma_desc);

#ifdef __cplusplus
}  // extern "C"
#endif

class ScopedDmaInitializer {
  int channel_count_;
  void** dma_desc_ = nullptr;
 public:
  ScopedDmaInitializer(int channel_count) : channel_count_(channel_count) {
    dma_desc_ = halide_init_dma(channel_count_);
  }

  ScopedDmaInitializer() = delete;
  ScopedDmaInitializer(const ScopedDmaInitializer&) = delete;
  ScopedDmaInitializer& operator=(const ScopedDmaInitializer&) = delete;
  ScopedDmaInitializer(ScopedDmaInitializer&&) = delete;

  ~ScopedDmaInitializer() {
    if (dma_desc_ != nullptr) {
      halide_release_dma(channel_count_, dma_desc_);
    }
  }

  bool is_valid() const { return dma_desc_ != nullptr; }
};

)INLINE_CODE";

    stream << headers;
}

void CodeGen_Xtensa::compile(const Module &module) {
    CodeGen_C::compile(module);
}

void CodeGen_Xtensa::compile(const Buffer<> &buffer) {
    CodeGen_C::compile(buffer);
}
void CodeGen_Xtensa::compile(const LoweredFunc &f, const std::map<std::string, std::string> &metadata_name_map) {
    // Don't put non-external function declarations in headers.
    if (is_header_or_extern_decl() && f.linkage == LinkageType::Internal) {
        return;
    }

    const std::vector<LoweredArgument> &args = f.args;

    have_user_context = false;
    for (const auto &arg : args) {
        // TODO: check that its type is void *?
        have_user_context |= (arg.name == "__user_context");
    }

    NameMangling name_mangling = f.name_mangling;
    if (name_mangling == NameMangling::Default) {
        name_mangling = (target.has_feature(Target::CPlusPlusMangling) ? NameMangling::CPlusPlus : NameMangling::C);
    }

    set_name_mangling_mode(name_mangling);

    std::vector<std::string> namespaces;
    std::string simple_name = extract_namespaces(f.name, namespaces);
    if (!is_c_plus_plus_interface()) {
        user_assert(namespaces.empty()) << "Namespace qualifiers not allowed on function name if not compiling with Target::CPlusPlusNameMangling.\n";
    }

    if (!namespaces.empty()) {
        for (const auto &ns : namespaces) {
            stream << "namespace " << ns << " {\n";
        }
        stream << "\n";
    }

    Stmt body = match_xtensa_patterns(f.body, target);

    const auto emit_arg_decls = [&](const Type &ucon_type = Type()) {
        const char *comma = "";
        for (const auto &arg : args) {
            stream << comma;
            if (arg.is_buffer()) {
                stream << "struct halide_buffer_t *"
                       << print_name(arg.name)
                       << "_buffer";
            } else {
                // If this arg is the user_context value, *and* ucon_type is valid,
                // use ucon_type instead of arg.type.
                const Type &t = (arg.name == "__user_context" && ucon_type.bits() != 0) ? ucon_type : arg.type;
                stream << print_type(t, AppendSpace) << print_name(arg.name);
            }
            comma = ", ";
        }
    };

    // Emit the function prototype
    if (f.linkage == LinkageType::Internal) {
        // If the function isn't public, mark it static.
        stream << "static ";
    }
    stream << "HALIDE_FUNCTION_ATTRS\n";
    stream << "int " << simple_name << "(";
    emit_arg_decls();

    if (is_header_or_extern_decl()) {
        stream << ");\n";
    } else {
        stream << ") ";
        open_scope();

        if (uses_gpu_for_loops) {
            stream << get_indent() << "halide_error("
                   << (have_user_context ? "__user_context_" : "nullptr")
                   << ", \"C++ Backend does not support gpu_blocks() or gpu_threads() yet, "
                   << "this function will always fail at runtime\");\n";
            stream << get_indent() << "return halide_error_code_device_malloc_failed;\n";
        } else {
            // Emit a local user_context we can pass in all cases, either
            // aliasing __user_context or nullptr.
            stream << get_indent() << "void * const _ucon = "
                   << (have_user_context ? "const_cast<void *>(__user_context)" : "nullptr")
                   << ";\n";

            if (target.has_feature(Target::NoAsserts)) {
                stream << get_indent() << "halide_maybe_unused(_ucon);";
            }

            UsesDmaCopy uses_dma;
            body.accept(&uses_dma);
            if (uses_dma.uses_dma) {
                stream << get_indent() << "ScopedDmaInitializer dma_initializer(" << uses_dma.max_channel_no + 1 << ");\n";
                stream << get_indent() << "if (!dma_initializer.is_valid()) {\n";
                stream << get_indent() << "halide_error(_ucon, \"DMA initialization failed\");\n";
                stream << get_indent() << "return halide_error_code_generic_error;\n";
                stream << get_indent() << "}\n";
            }
            // stream << "printf(\"" << simple_name << "\\n\");";
            // Emit the body
            print(body);
            // stream << "printf(\"[end]" << simple_name << "\\n\");";

            // Return success.
            stream << get_indent() << "return 0;\n";
            cache.clear();
        }

        // Ensure we use open/close_scope, so that the cache doesn't try to linger
        // across function boundaries for internal closures.
        close_scope("");
    }

    // Workaround for https://github.com/halide/Halide/issues/635:
    // For historical reasons, Halide-generated AOT code
    // defines user_context as `void const*`, but expects all
    // define_extern code with user_context usage to use `void *`. This
    // usually isn't an issue, but if both the caller and callee of the
    // pass a user_context, *and* c_plus_plus_name_mangling is enabled,
    // we get link errors because of this dichotomy. Fixing this
    // "correctly" (ie so that everything always uses identical types for
    // user_context in all cases) will require a *lot* of downstream
    // churn (see https://github.com/halide/Halide/issues/7298),
    // so this is a workaround: Add a wrapper with `void*`
    // ucon -> `void const*` ucon. In most cases this will be ignored
    // (and probably dead-stripped), but in these cases it's critical.
    //
    // (Note that we don't check to see if c_plus_plus_name_mangling is
    // enabled, since that would have to be done on the caller side, and
    // this is purely a callee-side fix.)
    if (f.linkage != LinkageType::Internal &&
        output_kind == CPlusPlusImplementation &&
        target.has_feature(Target::CPlusPlusMangling) &&
        get_target().has_feature(Target::UserContext)) {

        Type ucon_type = Type();
        for (const auto &arg : args) {
            if (arg.name == "__user_context") {
                ucon_type = arg.type;
                break;
            }
        }
        if (ucon_type == type_of<void const *>()) {
            stream << "\nHALIDE_FUNCTION_ATTRS\n";
            stream << "int " << simple_name << "(";
            emit_arg_decls(type_of<void *>());
            stream << ") ";
            open_scope();
            stream << get_indent() << "    return " << simple_name << "(";
            const char *comma = "";
            for (const auto &arg : args) {
                if (arg.name == "__user_context") {
                    // Add an explicit cast here so we won't call ourselves into oblivion
                    stream << "(void const *)";
                }
                stream << comma << print_name(arg.name);
                if (arg.is_buffer()) {
                    stream << "_buffer";
                }
                comma = ", ";
            }
            stream << ");\n";
            close_scope("");
        }
    }

    if (f.linkage == LinkageType::ExternalPlusArgv || f.linkage == LinkageType::ExternalPlusMetadata) {
        // Emit the argv version
        emit_argv_wrapper(simple_name, args);
    }

    if (f.linkage == LinkageType::ExternalPlusMetadata) {
        // Emit the metadata.
        emit_metadata_getter(simple_name, args, metadata_name_map);
    }

    if (!namespaces.empty()) {
        stream << "\n";
        for (size_t i = namespaces.size(); i > 0; i--) {
            stream << "}  // namespace " << namespaces[i - 1] << "\n";
        }
        stream << "\n";
    }
}

void CodeGen_Xtensa::add_vector_typedefs(const std::set<Type> &vector_types) {
    stream << R"INLINE_CODE(
#if defined(__XTENSA__)
#include <xtensa/sim.h>
#include <xtensa/tie/xt_ivpn.h>
#include <xtensa/tie/xt_timer.h>

// This inline function is needed by application to get the cycle count from ISS
inline int GetCycleCount() {
  return XT_RSR_CCOUNT();
}

#endif
)INLINE_CODE";

    if (!vector_types.empty()) {
        const char *native_typedef_decl = R"INLINE_CODE(


#include <xtensa/tie/xt_ivpn.h>

#define HALIDE_MAYBE_UNUSED __attribute__ ((unused))

typedef int8_t common_int8x64_t __attribute__((ext_vector_type(64)));
typedef uint8_t common_uint8x64_t __attribute__((ext_vector_type(64)));
typedef int16_t common_int16x32_t __attribute__((ext_vector_type(32)));
typedef uint16_t common_uint16x32_t __attribute__((ext_vector_type(32)));
typedef int32_t common_int32x16_t __attribute__((ext_vector_type(16)));
typedef uint32_t common_uint32x16_t __attribute__((ext_vector_type(16)));

using float16_t = xb_f16;
using native_vector_i8 = xb_vec2Nx8;
using native_vector_u8 = xb_vec2Nx8U;
using native_mask_i8 = vbool2N;
using native_vector_i16 = xb_vecNx16;
using native_vector_u16 = xb_vecNx16U;
using native_mask_i16 = vboolN;
using native_vector_i24 = xb_vec2Nx24;
using native_vector_i32 = xb_vecN_2x32v;
using native_vector_u32 = xb_vecN_2x32Uv;
using native_mask_i32 = vboolN_2;
using native_vector_i48 = xb_vecNx48;
using native_vector_f16 = xb_vecNxf16;
using native_vector_f32 = xb_vecN_2xf32;
using native_vector_i64 = xb_vecN_2x64w;

#if XCHAL_VISION_TYPE == 7
using int8x64_t = xb_vec2Nx8;
using uint8x64_t = xb_vec2Nx8U;
using int16x32_t = xb_vecNx16;
using uint16x32_t = xb_vecNx16U;
using int24_t = xb_int24;
using int24x64_t = xb_vec2Nx24;
using uint24x64_t = xb_vec2Nx24;
using int32x16_t = xb_vecN_2x32v;
using uint32x16_t = xb_vecN_2x32Uv;
using int48_t = xb_int48;
using int48x32_t = xb_vecNx48;
using uint48x32_t = xb_vecNx48;
using int64x16_t = xb_vecN_2x64w;
using uint1x16_t = vboolN_2;
using uint1x32_t = vboolN;
using uint1x64_t = vbool2N;
using float16x16_t = xb_vecN_2xf16;
using float16x32_t = xb_vecNxf16;
using float32x16_t = xb_vecN_2xf32;
#elif XCHAL_VISION_TYPE == 8
using int8x128_t = xb_vec2Nx8;
using uint8x128_t = xb_vec2Nx8U;
using int16x64_t = xb_vecNx16;
using uint16x64_t = xb_vecNx16U;
using int24_t = xb_int24;
using int24x128_t = xb_vec2Nx24;
using uint24x128_t = xb_vec2Nx24;
using int32x32_t = xb_vecN_2x32v;
using uint32x32_t = xb_vecN_2x32Uv;
using int48_t = xb_int48;
using int48x64_t = xb_vecNx48;
using uint48x64_t = xb_vecNx48;
using uint1x32_t = vboolN_2;
using uint1x64_t = vboolN;
using uint1x128_t = vbool2N;
using float16x32_t = xb_vecN_2xf16;
using float16x64_t = xb_vecNxf16;
using float32x32_t = xb_vecN_2xf32;
using int64x32_t = xb_vecN_2x64w;
#endif

using int8x4_t = xb_int32pr;
using uint8x4_t = xb_int32pr;
using int8x8_t = xb_int64pr;
using uint8x8_t = xb_int64pr;

template <typename NativeVector, int N>
struct MultipleOfNativeVector {
  NativeVector  __attribute__((aligned(XCHAL_VISION_SIMD8))) native_vector[N];

  MultipleOfNativeVector() {}

  // TODO(vksnk): figure out a better/safer way to construct it.
  enum FromCppVector { from_native_vector };
  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2) {
      static_assert(N == 2, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3) {
      static_assert(N == 3, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
  }

  inline MultipleOfNativeVector(FromCppVector, const MultipleOfNativeVector<NativeVector, 2> &src1, const MultipleOfNativeVector<NativeVector, 2> &src2) {
      static_assert(N == 4, "Wrong kind of constructor");
      native_vector[0] = src1.native_vector[0];
      native_vector[1] = src1.native_vector[1];
      native_vector[2] = src2.native_vector[0];
      native_vector[3] = src2.native_vector[1];
}

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4) {
      static_assert(N == 4, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                const NativeVector &src5, const NativeVector &src6) {
      static_assert(N == 6, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
      native_vector[4] = src5;
      native_vector[5] = src6;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                                const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8) {
      static_assert(N == 8, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
      native_vector[4] = src5;
      native_vector[5] = src6;
      native_vector[6] = src7;
      native_vector[7] = src8;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                                const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8,
                                                const NativeVector &src9, const NativeVector &src10, const NativeVector &src11, const NativeVector &src12) {
      static_assert(N == 12, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
      native_vector[4] = src5;
      native_vector[5] = src6;
      native_vector[6] = src7;
      native_vector[7] = src8;
      native_vector[8] = src9;
      native_vector[9] = src10;
      native_vector[10] = src11;
      native_vector[11] = src12;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4,
                                                const NativeVector &src5, const NativeVector &src6, const NativeVector &src7, const NativeVector &src8,
                                                const NativeVector &src9, const NativeVector &src10, const NativeVector &src11, const NativeVector &src12,
                                                const NativeVector &src13, const NativeVector &src14, const NativeVector &src15, const NativeVector &src16) {
      static_assert(N == 16, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
      native_vector[4] = src5;
      native_vector[5] = src6;
      native_vector[6] = src7;
      native_vector[7] = src8;
      native_vector[8] = src9;
      native_vector[9] = src10;
      native_vector[10] = src11;
      native_vector[11] = src12;
      native_vector[12] = src13;
      native_vector[13] = src14;
      native_vector[14] = src15;
      native_vector[15] = src16;
  }

};

#if XCHAL_VISION_TYPE == 7
using uint1x96_t = MultipleOfNativeVector<uint1x32_t, 3>;
using uint1x256_t = MultipleOfNativeVector<uint1x64_t, 4>;
using int8x128_t = MultipleOfNativeVector<int8x64_t, 2>;
using int8x192_t = MultipleOfNativeVector<int8x64_t, 3>;
using int8x256_t = MultipleOfNativeVector<int8x64_t, 4>;
using uint8x128_t = MultipleOfNativeVector<uint8x64_t, 2>;
using uint8x192_t = MultipleOfNativeVector<uint8x64_t, 3>;
using uint8x256_t = MultipleOfNativeVector<uint8x64_t, 4>;
using int16x64_t = MultipleOfNativeVector<int16x32_t, 2>;
using uint16x64_t = MultipleOfNativeVector<uint16x32_t, 2>;
using int16x96_t = MultipleOfNativeVector<int16x32_t, 3>;
using uint16x96_t = MultipleOfNativeVector<uint16x32_t, 3>;
using int16x128_t = MultipleOfNativeVector<int16x32_t, 4>;
using uint16x128_t = MultipleOfNativeVector<uint16x32_t, 4>;
using int24x128_t = MultipleOfNativeVector<int24x64_t, 2>;
using int32x32_t = MultipleOfNativeVector<int32x16_t, 2>;
using int32x48_t = MultipleOfNativeVector<int32x16_t, 3>;
using uint32x32_t = MultipleOfNativeVector<uint32x16_t, 2>;
using uint32x48_t = MultipleOfNativeVector<uint32x16_t, 3>;
using int32x64_t = MultipleOfNativeVector<int32x16_t, 4>;
using uint32x64_t = MultipleOfNativeVector<uint32x16_t, 4>;
using int32x96_t = MultipleOfNativeVector<int32x16_t, 6>;
using uint32x96_t = MultipleOfNativeVector<uint32x16_t, 6>;
using int32x128_t = MultipleOfNativeVector<int32x16_t, 8>;
using uint32x128_t = MultipleOfNativeVector<uint32x16_t, 8>;
// TODO(vksnk): this one should be generated automatically, but isn't.
using int32x192_t = MultipleOfNativeVector<int32x16_t, 12>;
using int32x256_t = MultipleOfNativeVector<int32x16_t, 16>;
using int48x64_t = MultipleOfNativeVector<int48x32_t, 2>;
using int64x32_t = MultipleOfNativeVector<int64x16_t, 2>;
using float32x32_t = MultipleOfNativeVector<float32x16_t, 2>;
using float32x48_t = MultipleOfNativeVector<float32x16_t, 3>;
using float32x64_t = MultipleOfNativeVector<float32x16_t, 4>;
#elif XCHAL_VISION_TYPE == 8
using uint1x192_t = MultipleOfNativeVector<uint1x64_t, 3>;
using uint1x512_t = MultipleOfNativeVector<uint1x128_t, 4>;
using int8x256_t = MultipleOfNativeVector<int8x128_t, 2>;
using int8x512_t = MultipleOfNativeVector<int8x128_t, 4>;
using uint8x256_t = MultipleOfNativeVector<uint8x128_t, 2>;
using uint8x384_t = MultipleOfNativeVector<uint8x128_t, 3>;
using uint8x512_t = MultipleOfNativeVector<uint8x128_t, 4>;
using int16x128_t = MultipleOfNativeVector<int16x64_t, 2>;
using uint16x128_t = MultipleOfNativeVector<uint16x64_t, 2>;
using int16x192_t = MultipleOfNativeVector<int16x64_t, 3>;
using uint16x192_t = MultipleOfNativeVector<uint16x64_t, 3>;
using int16x256_t = MultipleOfNativeVector<int16x64_t, 4>;
using uint16x256_t = MultipleOfNativeVector<uint16x64_t, 4>;
using int24x256_t = MultipleOfNativeVector<int24x128_t, 2>;
using int32x64_t = MultipleOfNativeVector<int32x32_t, 2>;
using uint32x64_t = MultipleOfNativeVector<uint32x32_t, 2>;
using int32x128_t = MultipleOfNativeVector<int32x32_t, 4>;
using uint32x128_t = MultipleOfNativeVector<uint32x32_t, 4>;
using int32x192_t = MultipleOfNativeVector<int32x32_t, 6>;
using uint32x192_t = MultipleOfNativeVector<uint32x32_t, 6>;
using int32x256_t = MultipleOfNativeVector<int32x32_t, 8>;
using uint32x256_t = MultipleOfNativeVector<uint32x32_t, 8>;
// TODO(vksnk): this one should be generated automatically, but isn't.
using int32x382_t = MultipleOfNativeVector<int32x32_t, 12>;
using int32x512_t = MultipleOfNativeVector<int32x32_t, 16>;
using int48x128_t = MultipleOfNativeVector<int48x64_t, 2>;
using int64x64_t = MultipleOfNativeVector<int64x32_t, 2>;
using float32x64_t = MultipleOfNativeVector<float32x32_t, 2>;
using float32x128_t = MultipleOfNativeVector<float32x32_t, 4>;
#endif

#if XCHAL_VISION_TYPE == 7
#define VECTOR_WIDTH_I8 64
#define VECTOR_WIDTH_U8 64
#define VECTOR_WIDTH_I16 32
#define VECTOR_WIDTH_U16 32
#define VECTOR_WIDTH_F16 32
#define VECTOR_WIDTH_I32 16
#define VECTOR_WIDTH_U32 16
#define VECTOR_WIDTH_F32 16
#elif XCHAL_VISION_TYPE == 8
#define VECTOR_WIDTH_I8 128
#define VECTOR_WIDTH_U8 128
#define VECTOR_WIDTH_I16 64
#define VECTOR_WIDTH_U16 64
#define VECTOR_WIDTH_F16 64
#define VECTOR_WIDTH_I32 32
#define VECTOR_WIDTH_U32 32
#define VECTOR_WIDTH_F32 32
#endif

using native_vector_i8_x2 = MultipleOfNativeVector<native_vector_i8, 2>;
using native_vector_i8_x3 = MultipleOfNativeVector<native_vector_i8, 3>;
using native_vector_i8_x4 = MultipleOfNativeVector<native_vector_i8, 4>;

using native_vector_u8_x2 = MultipleOfNativeVector<native_vector_u8, 2>;
using native_vector_u8_x3 = MultipleOfNativeVector<native_vector_u8, 3>;
using native_vector_u8_x4 = MultipleOfNativeVector<native_vector_u8, 4>;

using native_vector_i16_x2 = MultipleOfNativeVector<native_vector_i16, 2>;
using native_vector_i16_x4 = MultipleOfNativeVector<native_vector_i16, 4>;

using native_vector_u16_x2 = MultipleOfNativeVector<native_vector_u16, 2>;
using native_vector_u16_x3 = MultipleOfNativeVector<native_vector_u16, 3>;
using native_vector_u16_x4 = MultipleOfNativeVector<native_vector_u16, 4>;

using native_vector_i24_x2 = MultipleOfNativeVector<native_vector_i24, 2>;

using native_vector_i32_x2 = MultipleOfNativeVector<native_vector_i32, 2>;
using native_vector_i32_x4 = MultipleOfNativeVector<native_vector_i32, 4>;
using native_vector_i32_x6 = MultipleOfNativeVector<native_vector_i32, 6>;
using native_vector_i32_x8 = MultipleOfNativeVector<native_vector_i32, 8>;
using native_vector_i32_x16 = MultipleOfNativeVector<native_vector_i32, 16>;

using native_vector_u32_x2 = MultipleOfNativeVector<native_vector_u32, 2>;
using native_vector_u32_x4 = MultipleOfNativeVector<native_vector_u32, 4>;

using native_vector_i48_x2 = MultipleOfNativeVector<native_vector_i48, 2>;

using native_vector_f32_x2 = MultipleOfNativeVector<native_vector_f32, 2>;
using native_vector_f32_x4 = MultipleOfNativeVector<native_vector_f32, 4>;

using native_vector_i64_x2 = MultipleOfNativeVector<native_vector_i64, 2>;

using native_mask_i8_x4 = MultipleOfNativeVector<native_mask_i8, 4>;
using native_mask_i16_x3 = MultipleOfNativeVector<native_mask_i16, 3>;


template <typename ToType, typename FromType>
HALIDE_ALWAYS_INLINE ToType convert(const FromType& from_type) = delete;

template <typename ResultType>
HALIDE_ALWAYS_INLINE ResultType ramp(int32_t base, int32_t stride) = delete;

template <typename ResultType>
HALIDE_ALWAYS_INLINE ResultType dense_ramp(int32_t base) = delete;

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 ramp<native_vector_i32_x2>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
            IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 dense_ramp<native_vector_i32_x2>(int32_t base) {
    const native_vector_i32 base_w = native_vector_i32(base) + IVP_SEQN_2X32();
    const native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, base_w, base_w + lanes_2);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 ramp<native_vector_i32_x4>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector,
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_3 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_4 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 dense_ramp<native_vector_i32_x4>(int32_t base) {
    native_vector_i32 base_w = IVP_ADDN_2X32(native_vector_i32(base), IVP_SEQN_2X32());
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector,
                        base_w,
                        IVP_ADDN_2X32(base_w, lanes_2),
                        IVP_ADDN_2X32(base_w, lanes_3),
                        IVP_ADDN_2X32(base_w, lanes_4));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x8 ramp<native_vector_i32_x8>(int32_t base, int32_t stride) {
    native_vector_i32 one_to_n = IVP_SEQN_2X32();
    native_vector_i32 base_w = base;
    native_vector_i32 stride_w = stride;
    native_vector_i32 lanes_2 = VECTOR_WIDTH_I32;
    native_vector_i32 lanes_3 = VECTOR_WIDTH_I32 * 2;
    native_vector_i32 lanes_4 = VECTOR_WIDTH_I32 * 3;
    native_vector_i32 lanes_5 = VECTOR_WIDTH_I32 * 4;
    native_vector_i32 lanes_6 = VECTOR_WIDTH_I32 * 5;
    native_vector_i32 lanes_7 = VECTOR_WIDTH_I32 * 6;
    native_vector_i32 lanes_8 = VECTOR_WIDTH_I32 * 7;

    return native_vector_i32_x8(native_vector_i32_x8::from_native_vector,
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_3 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_4 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_5 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_6 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_7 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_8 + one_to_n, stride_w))));
}

template <typename ResultType, typename BaseType>
HALIDE_ALWAYS_INLINE ResultType broadcast(BaseType value) = delete;

template <>
HALIDE_ALWAYS_INLINE uint8x4_t broadcast<uint8x4_t, uint8_t>(uint8_t value) {
    native_vector_u8 v = value;
    return IVP_EXTRPRN_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template <>
HALIDE_ALWAYS_INLINE uint8x8_t broadcast<uint8x8_t, uint8_t>(uint8_t value) {
    native_vector_u8 v = value;
    return IVP_EXTRPR64N_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType aligned_load(const void *base, int32_t offset) {
    return *((const VectorType *)((const BaseType*)base + offset));
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load(const void *base, int32_t offset) {
    VectorType r;
    memcpy(&r, ((const BaseType*)base + offset), sizeof(BaseType) * Lanes);
    return r;
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void aligned_store(const VectorType& a, void *base, int32_t offset) {
    *((VectorType *)((BaseType*)base + offset)) = a;
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store(const VectorType& a, void *base, int32_t offset) {
    memcpy(((BaseType*)base + offset), &a, sizeof(BaseType) * Lanes);
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load_variable(const void *base, int32_t offset, int32_t count) {
    VectorType r;
    memcpy(&r, ((const BaseType*)base + offset), sizeof(BaseType) * count);
    return r;
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_variable(const VectorType& a, void *base, int32_t offset, int32_t count) {
    memcpy(((BaseType*)base + offset), &a, sizeof(BaseType) * count);
}

template <>
HALIDE_ALWAYS_INLINE void store_variable<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8& a, void *base, int32_t offset, int32_t count) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8U* __restrict ptr  = (xb_vec2Nx8U*)((uint8_t*)base + offset);
    IVP_SAV2NX8U_XP(a, align, ptr, count);
    IVP_SAPOS2NX8U_FP(align, ptr);
}

template <typename VectorType, typename OffsetType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_scatter(const VectorType& a, void *base, const OffsetType& offset) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[Lanes];
    aligned_store<VectorType, BaseType, Lanes>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[Lanes];
    aligned_store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);

    for (int i = 0; i < Lanes; i++) {
        ((BaseType*)base)[offsets[i]] = tmp[i];
    }
}

template <typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load_predicated(const void *base, const OffsetType& offset, const PredicateType& predicate) = delete;

template <>
HALIDE_ALWAYS_INLINE native_vector_u8 load_predicated<native_vector_u8, native_vector_i32_x4, native_mask_i8, uint8_t, VECTOR_WIDTH_U8>(const void *base, const native_vector_i32_x4& offset, const native_mask_i8& predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x4, int32_t, VECTOR_WIDTH_U8>(offset, &offsets[0], 0);
    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(vmask, &mask[0], 0);

    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_U8];
    for (int i = 0; i < VECTOR_WIDTH_U8; i++) {
        if (mask[i] == 1) {
            output[i] = ((const uint8_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_u8 *)output);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i16 load_predicated<native_vector_i16, native_vector_i32_x2, native_mask_i16, int16_t, VECTOR_WIDTH_I16>(const void *base, const native_vector_i32_x2& offset, const native_mask_i16& predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_I16];
    aligned_store<native_vector_i32_x2, int32_t, VECTOR_WIDTH_I16>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_I16];
    aligned_store<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(vmask, &mask[0], 0);

    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_I16];
    for (int i = 0; i < VECTOR_WIDTH_I16; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int16_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i16 *)output);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_u16 load_predicated<native_vector_u16, native_vector_i32_x2, native_mask_i16, uint16_t, VECTOR_WIDTH_U16>(const void *base, const native_vector_i32_x2& offset, const native_mask_i16& predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U16];
    aligned_store<native_vector_i32_x2, int32_t, VECTOR_WIDTH_U16>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U16];
    aligned_store<native_vector_i16, int16_t, VECTOR_WIDTH_U16>(vmask, &mask[0], 0);

    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[VECTOR_WIDTH_U16];
    for (int i = 0; i < VECTOR_WIDTH_U16; i++) {
        if (mask[i] == 1) {
            output[i] = ((const uint16_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_u16 *)output);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 load_predicated<native_vector_i32_x2, native_vector_i32_x2, native_mask_i16, int32_t, 2 * VECTOR_WIDTH_I32>(const void *base, const native_vector_i32_x2& offset, const native_mask_i16& predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);
    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[2 * VECTOR_WIDTH_I32];
    for (int i = 0; i < 2 * VECTOR_WIDTH_I32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int32_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i32_x2 *)output);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 load_predicated<native_vector_i32_x4, native_vector_i32_x4, native_mask_i8, int32_t, 4 * VECTOR_WIDTH_I32>(const void *base, const native_vector_i32_x4& offset, const native_mask_i8& predicate) {
    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);
    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_u8, uint8_t, 4 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) output[4 * VECTOR_WIDTH_I32];
    for (int i = 0; i < 4 * VECTOR_WIDTH_I32; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int32_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((native_vector_i32_x4 *)output);
}

template <typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_predicated(const VectorType& a, void *base, const OffsetType& offset, const PredicateType& predicate) = delete;

template <>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u8, native_vector_i32_x4, native_mask_i8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8& a, void *base, const native_vector_i32_x4& offset, const native_mask_i8& predicate) {
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x4, int32_t, VECTOR_WIDTH_U8>(offset, &offsets[0], 0);

    native_vector_u8 vmask = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate);
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(vmask, &mask[0], 0);

    for (int i = 0; i < VECTOR_WIDTH_U8; i++) {
        if (mask[i]) {
            ((uint8_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

template <>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u8_x4, native_vector_i32_x16, native_mask_i8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(const native_vector_u8_x4& a, void *base, const native_vector_i32_x16& offset, const native_mask_i8_x4& predicate) {
    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_i32_x16, int32_t, 4 * VECTOR_WIDTH_U8>(offset, &offsets[0], 0);

    native_vector_u8 vmask0 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[0]);
    native_vector_u8 vmask1 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[1]);
    native_vector_u8 vmask2 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[2]);
    native_vector_u8 vmask3 = IVP_MOV2NX8T(native_vector_u8(1), native_vector_u8(0), predicate.native_vector[3]);

    uint8_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[4 * VECTOR_WIDTH_U8];
    aligned_store<native_vector_u8_x4, uint8_t, 4 * VECTOR_WIDTH_U8>(
        native_vector_u8_x4(native_vector_u8_x4::from_native_vector, vmask0, vmask1, vmask2, vmask3), &mask[0], 0);

    for (int i = 0; i < 4 * VECTOR_WIDTH_U8; i++) {
        if (mask[i]) {
            ((uint8_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

template <>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_u16_x3, native_vector_i32_x6, native_mask_i16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(const native_vector_u16_x3& a, void *base, const native_vector_i32_x6& offset, const native_mask_i16_x3& predicate) {
    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_i32_x6, int32_t, 3 * VECTOR_WIDTH_U16>(offset, &offsets[0], 0);

    native_vector_u16 vmask0 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[0]);
    native_vector_u16 vmask1 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[1]);
    native_vector_u16 vmask2 = IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), predicate.native_vector[2]);

    uint16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[3 * VECTOR_WIDTH_U16];
    aligned_store<native_vector_u16_x3, uint16_t, 3 * VECTOR_WIDTH_U16>(
        native_vector_u16_x3(native_vector_u16_x3::from_native_vector, vmask0, vmask1, vmask2), &mask[0], 0);

    for (int i = 0; i < 3 * VECTOR_WIDTH_U16; i++) {
        if (mask[i]) {
            ((uint16_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

template <>
HALIDE_ALWAYS_INLINE void store_predicated<native_vector_i32_x2, native_vector_i32_x2, native_mask_i16, int32_t, 2 * VECTOR_WIDTH_I32>(const native_vector_i32_x2& a, void *base, const native_vector_i32_x2& offset, const native_mask_i16& predicate) {
    int32_t __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(a, &tmp[0], 0);

    int __attribute__((aligned(XCHAL_VISION_SIMD8))) offsets[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(offset, &offsets[0], 0);

    native_vector_i16 vmask = IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), predicate);
    int16_t __attribute__((aligned(XCHAL_VISION_SIMD8))) mask[2 * VECTOR_WIDTH_I32];
    aligned_store<native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I32>(vmask, &mask[0], 0);

    for (int i = 0; i < 2 * VECTOR_WIDTH_I32; i++) {
        if (mask[i]) {
            ((int32_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

inline uint8_t halide_shift_right(uint8_t a, uint8_t b) {
    return (uint16_t)a >> (uint16_t)b;
}

inline int8_t halide_shift_right(int8_t a, int8_t b) {
    return (int16_t)a >> (int16_t)b;
}

inline uint8_t halide_shift_left(uint8_t a, uint8_t b) {
    return (uint16_t)a << (uint16_t)b;
}

inline int8_t halide_shift_left(int8_t a, int8_t b) {
    return (int16_t)a << (int16_t)b;
}

template <typename VectorType, typename ScalarArgumentType, typename ScalarReturnType, int Lanes>
VectorType scalarize_unary(ScalarReturnType (*fn)(ScalarArgumentType), VectorType a) {
    ScalarArgumentType __attribute__((aligned(64))) tmp[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(a, &tmp[0], 0);

    for (int i = 0; i < Lanes; i++) {
        // Just update in-place, because it's a tmp buffer anyway.
        tmp[i] = fn(tmp[i]);
    }

    return *((VectorType *)tmp);
}

template <typename VectorType, typename ScalarArgumentType, typename ScalarReturnType, int Lanes>
VectorType scalarize_binary(ScalarReturnType (*fn)(ScalarArgumentType, ScalarArgumentType), VectorType a, VectorType b) {
    ScalarArgumentType __attribute__((aligned(64))) tmp_a[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(a, &tmp_a[0], 0);

    ScalarArgumentType __attribute__((aligned(64))) tmp_b[Lanes];
    aligned_store<VectorType, ScalarArgumentType, Lanes>(b, &tmp_b[0], 0);

    for (int i = 0; i < Lanes; i++) {
        // Just update in-place, because it's a tmp buffer anyway.
        tmp_a[i] = fn(tmp_a[i], tmp_b[i]);
    }

    return *((VectorType *)tmp_a);
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo shuffle(const VectorTypeFrom& a, const int32_t indices[LanesTo]) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp1[LanesFrom];
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp2[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, &tmp1[0], 0);
    for (int i = 0; i < LanesTo; i++) {
        tmp2[i] = tmp1[indices[i]];
    }

    return *((VectorTypeTo *)tmp2);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);

    return *((ResultType *)tmp);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b, const ArgType& c) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);

    return *((ResultType *)tmp);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b, const ArgType& c, const ArgType& d) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);
    store<ArgType, BaseType, LanesArg>(d, &tmp[0], 3 * LanesArg);

    return *((ResultType *)tmp);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 concat<native_vector_i32_x2, native_vector_i32, int32_t, 2 * VECTOR_WIDTH_I32, VECTOR_WIDTH_I32>(const native_vector_i32& a, const native_vector_i32& b) {
  return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 concat<native_vector_i32_x4, native_vector_i32, int32_t, 4 * VECTOR_WIDTH_I32, VECTOR_WIDTH_I32>(const native_vector_i32& a, const native_vector_i32& b, const native_vector_i32& c, const native_vector_i32& d) {
  return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, a, b, c, d);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 concat<native_vector_i16_x2, native_vector_i16, int16_t, 2 * VECTOR_WIDTH_I16, VECTOR_WIDTH_I16>(const native_vector_i16& a, const native_vector_i16& b) {
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 concat<native_vector_u16_x2, native_vector_u16, uint16_t, 2 * VECTOR_WIDTH_U16, VECTOR_WIDTH_U16>(const native_vector_u16& a, const native_vector_u16& b) {
  return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_u8_x2 concat<native_vector_u8_x2, native_vector_u8, uint8_t, 2 * VECTOR_WIDTH_U8, VECTOR_WIDTH_U8>(const native_vector_u8& a, const native_vector_u8& b) {
  return native_vector_u8_x2(native_vector_u8_x2::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 concat<native_vector_f32_x2, native_vector_f32, float, 2 * VECTOR_WIDTH_F32, VECTOR_WIDTH_F32>(const native_vector_f32& a, const native_vector_f32& b) {
  return native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_i24_x2 concat<native_vector_i24_x2, native_vector_i24, int24_t, 128, 64>(const native_vector_i24& a, const native_vector_i24& b) {
  return native_vector_i24_x2(native_vector_i24_x2::from_native_vector, a, b);
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_pad_to_native(const VectorTypeFrom& a, int lanes) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_slice_from_padded(const VectorTypeFrom& a, int lanes) {
    BaseType  __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[LanesFrom];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template <>
HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_slice_from_padded<native_vector_u16_x2, native_vector_u16, uint16_t, 2 * VECTOR_WIDTH_U16, VECTOR_WIDTH_U16>(const native_vector_u16_x2& a, int lanes) {
  return a.native_vector[0];
}

template <>
HALIDE_ALWAYS_INLINE native_mask_i16 halide_xtensa_pad_to_native<native_mask_i32, native_mask_i16, bool, VECTOR_WIDTH_I32, VECTOR_WIDTH_I16>(const native_mask_i32& a, int lanes) {
    return IVP_JOINBN_2(a, a);
}

template <>
HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_pad_to_native<native_mask_i16, native_mask_i8, bool, VECTOR_WIDTH_I16, VECTOR_WIDTH_I8>(const native_mask_i16& a, int lanes) {
    return IVP_JOINBN(a, a);
}

template <>
HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_pad_to_native<native_mask_i32, native_mask_i8, bool, VECTOR_WIDTH_I32, VECTOR_WIDTH_I8>(const native_mask_i32& a, int lanes) {
    return IVP_JOINBN(IVP_JOINBN_2(a, a), IVP_JOINBN_2(a, a));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int8x4_t load<int8x4_t, int8_t, 4>(const void *base, int32_t offset) {
    return *((const int8x4_t*)((const int8_t*)base + offset));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x4_t load<uint8x4_t, uint8_t, 4>(const void *base, int32_t offset) {
    return *((const uint8x4_t*)((const uint8_t*)base + offset));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u8 load<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const void *base, int32_t offset) {
    native_vector_u8 r;
    const xb_vec2Nx8U*  __restrict ptr = (const xb_vec2Nx8U*)((const uint8_t*)base + offset);
    IVP_L2U2NX8U_XP(r, ptr, 0);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i8, int8_t, VECTOR_WIDTH_I8>(const native_vector_i8& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8* __restrict ptr  = (xb_vec2Nx8*)((int8_t*)base + offset);
    IVP_SA2NX8_IP(a, align, ptr);
    IVP_SAPOS2NX8_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_u8, uint8_t, VECTOR_WIDTH_U8>(const native_vector_u8& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vec2Nx8U* __restrict ptr  = (xb_vec2Nx8U*)((uint8_t*)base + offset);
    IVP_SA2NX8U_IP(a, align, ptr);
    IVP_SAPOS2NX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 load<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8*  __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16_IP(r, align, (const xb_vecNx16*)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i16, int16_t, VECTOR_WIDTH_I16>(const native_vector_i16& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16* ptr = (xb_vecNx16*)((int16_t*)base + offset);
    IVP_SANX16_IP(a, align, ptr);
    // Flush alignment register.
    IVP_SAPOSNX16_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_i16_x2, int16_t, 2 * VECTOR_WIDTH_I16>(const native_vector_i16_x2& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16* ptr = (xb_vecNx16*)((int16_t*)base + offset);
    IVP_SANX16_IP(a.native_vector[0], align, ptr);
    IVP_SANX16_IP(a.native_vector[1], align, ptr);
    // Flush alignment register.
    IVP_SAPOSNX16_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16 load<native_vector_u16, uint16_t, VECTOR_WIDTH_U16>(const void *base, int32_t offset) {
    xb_vecNx16U r;
    const xb_vec2Nx8*  __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16U_IP(r, align, (const xb_vecNx16U*)ptr8);

    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<native_vector_u16, uint16_t, VECTOR_WIDTH_U16>(const native_vector_u16& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx16U* ptr  = (xb_vecNx16U*)((uint16_t*)base + offset);
    IVP_SANX16U_IP(a, align, ptr);
    IVP_SAPOSNX16U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16_x2 load<native_vector_i16_x2, int16_t, 2 * VECTOR_WIDTH_I16>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16_IP(r1, align, (const xb_vecNx16*)ptr8);
    IVP_LANX16_IP(r2, align, (const xb_vecNx16*)ptr8);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16_x2 load<native_vector_u16_x2, uint16_t, 2 * VECTOR_WIDTH_U16>(const void *base, int32_t offset) {
    xb_vecNx16U r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16U_IP(r1, align, (const xb_vecNx16U*)ptr8);
    IVP_LANX16U_IP(r2, align, (const xb_vecNx16U*)ptr8);

    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 load<native_vector_i32_x2, int32_t, 2 * VECTOR_WIDTH_I32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1;
    const xb_vecN_2x32v* __restrict ptr = (const xb_vecN_2x32v*)((const int32_t*)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, nv8_0, nv8_1);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x4 load<native_vector_i32_x4, int32_t, 4 * VECTOR_WIDTH_I32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1, nv8_2, nv8_3;
    const xb_vecN_2x32v* __restrict ptr = (const xb_vecN_2x32v*)((const int32_t*)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    IVP_LAN_2X32_IP(nv8_2, align, ptr);
    IVP_LAN_2X32_IP(nv8_3, align, ptr);
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, nv8_0, nv8_1, nv8_2, nv8_3);
}

template <typename ResultType, typename LoadType>
HALIDE_ALWAYS_INLINE ResultType widening_load(const void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 widening_load<native_vector_i16, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r, align, (const xb_vecNx8U*)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16_x2 widening_load<native_vector_i16_x2, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U*)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U*)ptr8);

    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16_x2 widening_load<native_vector_u16_x2, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U*)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U*)ptr8);

    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32 widening_load<native_vector_i32, int16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16*)ptr8);
    return r1;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 widening_load<native_vector_i32_x2, int16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16S_IP(r2, align, (const xb_vecN_2x16*)ptr8);

    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x2 widening_load<native_vector_i32_x2, uint16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);

    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u32_x2 widening_load<native_vector_u32_x2, uint16_t>(const void *base, int32_t offset) {
    native_vector_u32 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);

    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32_x4 widening_load<native_vector_i32_x4, uint16_t>(const void *base, int32_t offset) {
    native_vector_i32 r1, r2, r3, r4;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);
    IVP_LAN_2X16U_IP(r3, align, (const xb_vecN_2x16U*)ptr8);
    IVP_LAN_2X16U_IP(r4, align, (const xb_vecN_2x16U*)ptr8);

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, r1, r2, r3, r4);
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_narrowing(const VectorType& a, void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i16, int8_t, VECTOR_WIDTH_I16>(const native_vector_i16& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8* __restrict ptr  = (xb_vecNx8*)((int8_t*)base + offset);
    IVP_SANX8S_IP(a, align, ptr);
    IVP_SAPOSNX8S_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i16, uint8_t, VECTOR_WIDTH_I16>(const native_vector_i16& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8U* __restrict ptr  = (xb_vecNx8U*)((uint8_t*)base + offset);
    IVP_SANX8U_IP(a, align, ptr);
    IVP_SAPOSNX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_u16, uint8_t, VECTOR_WIDTH_U16>(const native_vector_u16& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecNx8U* __restrict ptr  = (xb_vecNx8U*)((uint8_t*)base + offset);
    IVP_SANX8U_IP(a, align, ptr);
    IVP_SAPOSNX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_i32, int16_t, VECTOR_WIDTH_I32>(const native_vector_i32& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecN_2x16* __restrict ptr  = (xb_vecN_2x16*)((int16_t*)base + offset);
    IVP_SAN_2X16S_IP(a, align, ptr);
    IVP_SAPOSN_2X16S_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<native_vector_u32, uint16_t, VECTOR_WIDTH_U32>(const native_vector_u32& a, void *base, int32_t offset) {
    valign align = IVP_ZALIGN();
    xb_vecN_2x16U* __restrict ptr  = (xb_vecN_2x16U*)((uint16_t*)base + offset);
    IVP_SAN_2X16U_IP(a, align, ptr);
    IVP_SAPOSN_2X16U_FP(align, ptr);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_interleave_i16(const native_vector_i16& a, const native_vector_i16& b) {
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE native_vector_i16_x4 halide_xtensa_interleave_i16(const native_vector_i16_x2& a, const native_vector_i16_x2& b) {
  return native_vector_i16_x4(native_vector_i16_x4::from_native_vector,
                                IVP_SELNX16I(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_HI),
                                IVP_SELNX16I(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_interleave_u16(const native_vector_u16& a, const native_vector_u16& b) {
  return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}

#if XCHAL_VISION_TYPE == 7
// This sequence of instructions is taken from the user guide.
HALIDE_ALWAYS_INLINE native_vector_u16_x3 halide_xtensa_interleave_u16(const native_vector_u16& a, const native_vector_u16& b, const native_vector_u16& c) {
  // 16-bit interleave patterns
  __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_0[64] = {
      0,  42, 1,  22, 32, 23, 2,  43, 3,  24, 33, 25, 4,  44, 5,  26,
      34, 27, 6,  45, 7,  28, 35, 29, 8,  46, 9,  30, 36, 31, 10, 47,
      11, 0,  37, 33, 12, 48, 13, 2,  38, 35, 14, 49, 15, 4,  39, 37,
      16, 50, 17, 6,  40, 39, 18, 51, 19, 8,  41, 41, 20, 52, 21, 10};
  __attribute__((aligned(XCHAL_VISION_SIMD8))) unsigned char int_16B_c3_step_1[64] = {
      11, 42, 53, 22, 12, 23, 13, 43, 54, 24, 14, 25, 15, 44, 55, 26,
      16, 27, 17, 45, 56, 28, 18, 29, 19, 46, 57, 30, 20, 31, 21, 47,
      58, 0,  22, 1,  23, 48, 59, 2,  24, 3,  25, 49, 60, 4,  26, 5,
      27, 50, 61, 6,  28, 7,  29, 51, 62, 8,  30, 9,  31, 52, 63, 10};
  unsigned long long int_16B_c3_step_1_msk = 0xffffffff55555555ULL;
  native_vector_u16 vRG0, vRG1, vRGB0, vRGB1, vRGB2;
  // interleave RG
  IVP_DSELNX16UI(vRG1, vRG0, b, a, IVP_DSELI_INTERLEAVE_1);
  // interleave RG, B
  IVP_DSELNX16U(vRGB1, vRGB0, c, vRG0, *((xb_vec2Nx8*)int_16B_c3_step_0));
  IVP_DSELNX16UT(vRGB1, vRGB2, c, vRG1, *((xb_vec2Nx8*)int_16B_c3_step_1),
                *((vbool2N*)&int_16B_c3_step_1_msk));

  return native_vector_u16_x3(native_vector_u16_x3::from_native_vector, vRGB0, vRGB1, vRGB2);
}
#endif

HALIDE_ALWAYS_INLINE native_vector_u16_x4 halide_xtensa_interleave_u16(const native_vector_u16_x2& a, const native_vector_u16_x2& b) {
  return native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                IVP_SELNX16UI(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b.native_vector[0], a.native_vector[0], IVP_SELI_16B_INTERLEAVE_1_HI),
                                IVP_SELNX16UI(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b.native_vector[1], a.native_vector[1], IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x4 halide_xtensa_interleave_u16(const native_vector_u16& a, const native_vector_u16& b, const native_vector_u16& c, const native_vector_u16& d) {
  const native_vector_u16 ab0 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO);
  const native_vector_u16 ab1 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI);
  const native_vector_u16 cd0 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_LO);
  const native_vector_u16 cd1 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_HI);


  return native_vector_u16_x4(native_vector_u16_x4::from_native_vector,
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_HI),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u8_x2 halide_xtensa_interleave_u8(const native_vector_u8& a, const native_vector_u8& b) {
  return native_vector_u8_x2(native_vector_u8_x2::from_native_vector,
                                IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO),
                                IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE native_vector_u8_x3 halide_xtensa_interleave_u8(
    const native_vector_u8& a, const native_vector_u8& b, const native_vector_u8& c) {
  native_vector_u8 vRG0, vRG1, vRGB0, vRGB1, vRGB2;
  IVP_DSEL2NX8UI(vRG1, vRG0, b, a, IVP_DSELI_8B_INTERLEAVE_1);
  IVP_DSEL2NX8UI(vRGB1, vRGB0, c, vRG0, IVP_DSELI_8B_INTERLEAVE_C3_STEP_0);
  IVP_DSEL2NX8UI_H(vRGB1, vRGB2, c, vRG1, IVP_DSELI_8B_INTERLEAVE_C3_STEP_1);
  return native_vector_u8_x3(native_vector_u8_x3::from_native_vector, vRGB0, vRGB1, vRGB2);
}

HALIDE_ALWAYS_INLINE native_vector_u8_x4 halide_xtensa_interleave_u8(const native_vector_u8& a, const native_vector_u8& b, const native_vector_u8& c, const native_vector_u8& d) {
  const native_vector_u8 ab0 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO);
  const native_vector_u8 ab1 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI);
  const native_vector_u8 cd0 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_LO);
  const native_vector_u8 cd1 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_HI);


  return native_vector_u8_x4(native_vector_u8_x4::from_native_vector,
                                IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_LO),
                                IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_HI),
                                IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_LO),
                                IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_mask_i8_x4 halide_xtensa_interleave_u1(const native_mask_i8& a, const native_mask_i8& b, const native_mask_i8& c, const native_mask_i8& d) {
    native_vector_u8 a8 = 0, b8 = 0, c8 = 0, d8 = 0;
    IVP_INJBI2NX8(a8, a, 0);
    IVP_INJBI2NX8(b8, b, 0);
    IVP_INJBI2NX8(c8, c, 0);
    IVP_INJBI2NX8(d8, d, 0);

    native_vector_u8_x4 interleaved8 = halide_xtensa_interleave_u8(a8, b8, c8, d8);

    native_mask_i8 ra = IVP_EXTBI2NX8(interleaved8.native_vector[0], 0);
    native_mask_i8 rb = IVP_EXTBI2NX8(interleaved8.native_vector[1], 0);
    native_mask_i8 rc = IVP_EXTBI2NX8(interleaved8.native_vector[2], 0);
    native_mask_i8 rd = IVP_EXTBI2NX8(interleaved8.native_vector[3], 0);

    return native_mask_i8_x4(native_mask_i8_x4::from_native_vector, ra, rb, rc, rd);
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_interleave_f32(const native_vector_f32& a, const native_vector_f32& b) {
  return native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                                IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE native_vector_f32_x4 halide_xtensa_interleave_f32(const native_vector_f32_x2& a, const native_vector_f32_x2& b) {
  return native_vector_f32_x4(native_vector_f32_x4::from_native_vector,
                                IVP_SELN_2XF32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b.native_vector[0], a.native_vector[0], IVP_SELI_32B_INTERLEAVE_1_HI),
                                IVP_SELN_2XF32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_LO),
                                IVP_SELN_2XF32I(b.native_vector[1], a.native_vector[1], IVP_SELI_32B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x4 halide_xtensa_interleave_f32(const native_vector_f32& a, const native_vector_f32& b,
                                                               const native_vector_f32& c, const native_vector_f32& d) {
  const native_vector_f32 ab0 = IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_LO);
  const native_vector_f32 ab1 = IVP_SELN_2XF32I(b, a, IVP_SELI_32B_INTERLEAVE_1_HI);
  const native_vector_f32 cd0 = IVP_SELN_2XF32I(d, c, IVP_SELI_32B_INTERLEAVE_1_LO);
  const native_vector_f32 cd1 = IVP_SELN_2XF32I(d, c, IVP_SELI_32B_INTERLEAVE_1_HI);


  return native_vector_f32_x4(native_vector_f32_x4::from_native_vector,
                                IVP_SELN_2XF32I(cd0, ab0, IVP_SELI_32B_INTERLEAVE_2_LO),
                                IVP_SELN_2XF32I(cd0, ab0, IVP_SELI_32B_INTERLEAVE_2_HI),
                                IVP_SELN_2XF32I(cd1, ab1, IVP_SELI_32B_INTERLEAVE_2_LO),
                                IVP_SELN_2XF32I(cd1, ab1, IVP_SELI_32B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_extract_0_of_3_u8(const native_vector_u8& a0, const native_vector_u8& a1, const native_vector_u8& a2) {
  // TODO(vksnk): there is likely a better way to do it.
  native_vector_u8 vR, vG, vB, vRG0, vRG1;
  IVP_DSEL2NX8UI(vB, vRG0, a1, a0, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_0);
  IVP_DSEL2NX8UI_H(vB, vRG1, a2, a1, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_1);
  IVP_DSEL2NX8UI (vG,vR, vRG1,vRG0, IVP_DSELI_8B_DEINTERLEAVE_1);
  return vR;
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_extract_0_of_3_u8(const native_vector_u8_x3& a) {
  return halide_xtensa_extract_0_of_3_u8(a.native_vector[0], a.native_vector[1], a.native_vector[2]);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_extract_0_of_3_i8(const native_vector_i8& a0, const native_vector_i8& a1, const native_vector_i8& a2) {
  // TODO(aelphy): there is likely a better way to do it.
  native_vector_i8 vR, vG, vB, vRG0, vRG1;
  IVP_DSEL2NX8I(vB, vRG0, a1, a0, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_0);
  IVP_DSEL2NX8I_H(vB, vRG1, a2, a1, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_1);
  IVP_DSEL2NX8I (vG,vR, vRG1,vRG0, IVP_DSELI_8B_DEINTERLEAVE_1);
  return vR;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_extract_0_of_3_i8(const native_vector_i8_x3& a) {
  return halide_xtensa_extract_0_of_3_i8(a.native_vector[0], a.native_vector[1], a.native_vector[2]);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_deinterleave_even_i16(const native_vector_i16_x2& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_deinterleave_odd_i16(const native_vector_i16_x2& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_deinterleave_even_i16(const native_vector_i16_x4& a) {
  return native_vector_i16_x2(
      native_vector_i16_x2::from_native_vector,
      halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_deinterleave_odd_i16(const native_vector_i16_x4& a) {
  return native_vector_i16_x2(
      native_vector_i16_x2::from_native_vector,
      halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_deinterleave_even_u16(const native_vector_u16_x2& a) {
  return  IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_deinterleave_odd_u16(const native_vector_u16_x2& a) {
  return  IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_deinterleave_even_u16(const native_vector_u16_x4& a) {
  return native_vector_u16_x2(
      native_vector_u16_x2::from_native_vector,
      halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_even_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_deinterleave_odd_u16(const native_vector_u16_x4& a) {
  return native_vector_u16_x2(
      native_vector_u16_x2::from_native_vector,
      halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_odd_u16(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_deinterleave_even_f32(const native_vector_f32_x2& a) {
  return  IVP_SELN_2XF32I(a.native_vector[1], a.native_vector[0], IVP_SELI_32B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_deinterleave_odd_f32(const native_vector_f32_x2& a) {
  return  IVP_SELN_2XF32I(a.native_vector[1], a.native_vector[0], IVP_SELI_32B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_deinterleave_even_f32(const native_vector_f32_x4& a) {
  return native_vector_f32_x2(
      native_vector_f32_x2::from_native_vector,
      halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_deinterleave_odd_f32(const native_vector_f32_x4& a) {
  return native_vector_f32_x2(
      native_vector_f32_x2::from_native_vector,
      halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
      halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3])));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_0_of_4_f32(const native_vector_f32_x4& a) {
  return halide_xtensa_deinterleave_even_f32(
          native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
          halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_1_of_4_f32(const native_vector_f32_x4& a) {
  return halide_xtensa_deinterleave_even_f32(
          native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
          halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_2_of_4_f32(const native_vector_f32_x4& a) {
  return halide_xtensa_deinterleave_odd_f32(
          native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
          halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_even_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_extract_3_of_4_f32(const native_vector_f32_x4& a) {
  return halide_xtensa_deinterleave_odd_f32(
          native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
          halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_odd_f32(native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_0_of_4_i16(const native_vector_i16_x4& a) {
  return halide_xtensa_deinterleave_even_i16(
          native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
          halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_1_of_4_i16(const native_vector_i16_x4& a) {
  return halide_xtensa_deinterleave_even_i16(
          native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
          halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_2_of_4_i16(const native_vector_i16_x4& a) {
  return halide_xtensa_deinterleave_odd_i16(
          native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
          halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_even_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_extract_3_of_4_i16(const native_vector_i16_x4& a) {
  return halide_xtensa_deinterleave_odd_i16(
          native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
          halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[0], a.native_vector[1])),
          halide_xtensa_deinterleave_odd_i16(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, a.native_vector[2], a.native_vector[3]))
        ));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_slice_i16(const native_vector_i16_x2& a, int start) {
  return IVP_SELNX16(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + native_vector_i16(start));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_slice_u16(const native_vector_u16_x2& a, int start) {
  return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + native_vector_i16(start));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_slice_i32(const native_vector_i32_x2& a, int start) {
  return IVP_SELN_2X32(a.native_vector[1], a.native_vector[0], IVP_SEQN_2X32() + native_vector_i32(start));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_slice_u32(const native_vector_u32_x2& a, int start) {
  return IVP_SELN_2X32U(a.native_vector[1], a.native_vector[0], IVP_SEQN_2X32() + native_vector_i32(start));
}

/*
HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_deinterleave_even_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_deinterleave_odd_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}
*/
HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_deinterleave_even_u8(const native_vector_u8_x2& a) {
  return  IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_deinterleave_odd_u8(const native_vector_u8_x2& a) {
  return  IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_slice_f32(const native_vector_f32_x2& a, int start) {
  return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], IVP_ADDN_2X32(IVP_SEQN_2X32(), native_vector_i32(start)));
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_dynamic_shuffle(const native_vector_u8_x2& a, const native_vector_i8& b) {
  return IVP_SEL2NX8(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_dynamic_shuffle(const native_vector_i16_x2& a, const native_vector_i16& b) {
  return IVP_SELNX16(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_dynamic_shuffle(const native_vector_u16_x2& a, const native_vector_i16& b) {
  return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_dynamic_shuffle(const native_vector_i16_x2& a, const native_vector_i16_x2& b) {
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                    IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                    IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[1])
                  );
}

HALIDE_ALWAYS_INLINE native_vector_u16_x2 halide_xtensa_dynamic_shuffle(const native_vector_u16_x2& a, const native_vector_i16_x2& b) {
  return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                    IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                    IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[1])
                  );
}

HALIDE_ALWAYS_INLINE native_vector_f32 halide_xtensa_dynamic_shuffle(const native_vector_f32_x2& a, const native_vector_i32& b) {
  return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_sat_add_i32(const native_vector_i32& a,
                                                                      const native_vector_i32& b) {
  // I am not 100% about it.
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = IVP_MULN_2X32(a, one);
  IVP_MULAN_2X32(l0, b, one);
  return IVP_PACKVRN_2X64W(l0, 0);
}

HALIDE_ALWAYS_INLINE native_vector_i32_x2 halide_xtensa_sat_add_i32(const native_vector_i32_x2& a,
                                                                      const native_vector_i32_x2& b) {
  // I am not 100% about it.
  xb_vecN_2x32v zero = 0;
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = a.native_vector[0] * one;
  IVP_MULAN_2X32(l0, b.native_vector[0], one);
  xb_vecN_2x64w l1 = a.native_vector[1] * one;
  IVP_MULAN_2X32(l1, b.native_vector[1], one);
  return native_vector_i32_x2(native_vector_i32_x2::from_native_vector, IVP_PACKVN_2X64W(l0, zero), IVP_PACKVN_2X64W(l1, zero));

}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_add_i16(const native_vector_i16& a, const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i16 r = a;
  IVP_ADDNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sub_i16(const native_vector_i16& a, const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i16 r = a;
  IVP_SUBNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_max_i16(const native_vector_i16& a, const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i16 r = a;
  IVP_MAXNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_min_i16(const native_vector_i16& a, const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i16 r = a;
  IVP_MINNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sat_add_i16(const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c, const native_vector_i16& a) {
  native_vector_i16 r = a;
  IVP_ADDSNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_pred_sat_sub_i16(const native_vector_i16& a, const native_mask_i16& p, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i16 r = a;
  IVP_SUBSNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_i64(const native_vector_i32& a, const native_vector_i32& b) {
  return IVP_MULN_2X32(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_add_i64(const native_vector_i64& r, const native_vector_i32& a, const native_vector_i32& b) {
  native_vector_i64 r1 = r;
  IVP_MULAN_2X32(r1, a, b);
  return r1;
}

HALIDE_ALWAYS_INLINE native_vector_i64 halide_xtensa_widen_mul_add_i64(const native_vector_i32& a, const native_vector_i32& b, const native_vector_i32& c) {
  xb_vecN_2x64w r = IVP_MULN_2X32(c, native_vector_i32(1));
  IVP_MULAN_2X32(r, a, b);
  return r;
}


HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_mul_add_i48(const native_vector_i48& a, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i48 r = a;
  IVP_MULANX16(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_u24(const native_vector_i24& a, const native_vector_u8& b, const native_vector_u8& c) {
  native_vector_i24 r = a;
  IVP_MULUUA2NX8(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_i24(const native_vector_i24& a, const native_vector_i8& b, const native_vector_i8& c) {
  native_vector_i24 r = a;
  IVP_MULA2NX8(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
                                            const native_vector_i24& acc,
                                            const native_vector_i8& a0,
                                            const int8_t& s0,
                                            const native_vector_i8& a1,
                                            const int8_t& s1,
                                            const native_vector_i8& a2,
                                            const int8_t& s2,
                                            const native_vector_i8& a3,
                                            const int8_t& s3
                                            ) {
  native_vector_i24 r = acc;
  const int8_t scalar_coef[] = {s3, s2, s1, s0};
  const xb_int32pr * __restrict coef = (const xb_int32pr*)scalar_coef;
  IVP_MULQA2N8XR8(r, a0, a1, a2, a3, coef[0]);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
                                            const native_vector_i24& acc,
                                            const native_vector_i8& a0,
                                            const native_vector_i8& a1,
                                            const native_vector_i8& a2,
                                            const native_vector_i8& a3,
                                            const int8x4_t& s
                                            ) {
  native_vector_i24 r = acc;
  IVP_MULQA2N8XR8(r, a3, a2, a1, a0, s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_i24(
                                            const native_vector_i24& acc,
                                            const native_vector_i8_x4& a,
                                            const int8x4_t& s
                                            ) {
  native_vector_i24 r = acc;
  IVP_MULQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_u24(
                                            const native_vector_i24& acc,
                                            const native_vector_u8& a0,
                                            const native_vector_u8& a1,
                                            const native_vector_u8& a2,
                                            const native_vector_u8& a3,
                                            const uint8x4_t& s
                                            ) {
  native_vector_i24 r = acc;
  IVP_MULUUQA2N8XR8(r, a3, a2, a1, a0, s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_u24(
                                            const native_vector_i24& acc,
                                            const native_vector_u8_x4& a,
                                            const uint8x4_t& s
                                            ) {
  native_vector_i24 r = acc;
  IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_quad_mul_add_by_scalar_u24(
                                            const native_vector_i24& acc,
                                            const native_vector_u8_x4& a,
                                            const uint8_t& s
                                            ) {
  const xb_int32pr coef = s | (s << 8) | (s << 16) | (s << 24);

  native_vector_i24 r = acc;
  IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], coef);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24_x2 halide_xtensa_dual_widen_quad_mul_add_i24(
                                            const native_vector_i24_x2& acc,
                                            const native_vector_i8_x4& a,
                                            const int8x8_t& s) {
  native_vector_i24_x2 r(acc);
  IVP_DMULQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24_x2 halide_xtensa_dual_widen_quad_mul_add_u24(
                                            const native_vector_i24_x2& acc,
                                            const native_vector_u8_x4& a,
                                            const uint8x8_t& s) {
  native_vector_i24_x2 r(acc);
  IVP_DMULUUQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_i24(const native_vector_i8& a, const native_vector_i8& b,
                                                                  const native_vector_i8& c, const native_vector_i8& d) {
  return IVP_MULP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_add_i24(const native_vector_i24& a, const native_vector_i8& b,
                                                                  const native_vector_i8& c, const native_vector_i8& d, const native_vector_i8& e) {
  native_vector_i24 r = a;
  IVP_MULPA2NX8(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_add_u24(const native_vector_i24& a, const native_vector_u8& b,
                                                                  const native_vector_u8& c, const native_vector_u8& d, const native_vector_u8& e) {
  native_vector_i24 r = a;
  IVP_MULUUPA2NX8(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_pair_mul_u24(const native_vector_u8& a, const native_vector_u8& b,
                                                                  const native_vector_u8& c, const native_vector_u8& d) {
  return IVP_MULUUP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_i48(const native_vector_i16& a, const native_vector_i16& b,
                                                                  const native_vector_i16& c, const native_vector_i16& d) {
  return IVP_MULPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_add_i48(const native_vector_i48& a, const native_vector_i16& b,
                                                                  const native_vector_i16& c, const native_vector_i16& d, const native_vector_i16& e) {
  native_vector_i48 r = a;
  IVP_MULPANX16(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_mul_u48(const native_vector_u16& a, const native_vector_u16& b,
                                                                  const native_vector_u16& c, const native_vector_u16& d) {
  return IVP_MULUUPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_mul_add_by_diff_u24(const native_vector_i24& a, const native_vector_u8& d1,
                                                                  const native_vector_u8& d2, const native_vector_u8& c) {
  native_vector_i24 r = a;
  IVP_MULUUPDA2NX8(r, d1, c, d2, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_i48(const native_vector_i16& a, const native_vector_i16& b) {
  return IVP_ADDWNX16(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_i48(const native_vector_i48& a, const native_vector_i16& b) {
  native_vector_i48 r = a;
  IVP_ADDWANX16(r, b, native_vector_i16(0));
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_add_i48(const native_vector_i48& a, const native_vector_i16& b, const native_vector_i16& c) {
  native_vector_i48 r = a;
  IVP_ADDWANX16(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_u48(const native_vector_u16& a, const native_vector_u16& b) {
  return IVP_ADDWUNX16U(a, b);
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_add_u48(const native_vector_i48& a, const native_vector_u16& b) {
  native_vector_i48 r = a;
  IVP_ADDWUANX16U(r, b, native_vector_u16(0));
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_quad_add_i48(
                                      const native_vector_i16& a, const native_vector_i16& b, 
                                      const native_vector_i16& c, const native_vector_i16& d) {
  native_vector_i48 r = IVP_ADDWNX16(a, b);
  IVP_ADDWANX16(r, c, d);
  return r;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_u16>(const native_vector_u16& src);

HALIDE_ALWAYS_INLINE native_vector_i64_x2 halide_xtensa_widen_right_mul_u64(const native_vector_u32_x2& a, const native_vector_u16 &b) {
  native_vector_u32_x2 b32 = convert<native_vector_u32_x2, native_vector_u16>(b);

  return native_vector_i64_x2(native_vector_i64_x2::from_native_vector,
    IVP_MULUSN_2X32(a.native_vector[0], xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(b32.native_vector[0])),
    IVP_MULUSN_2X32(a.native_vector[1], xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(b32.native_vector[1])));
}

HALIDE_ALWAYS_INLINE native_vector_i48 halide_xtensa_widen_pair_add_u48(const native_vector_i48& a, const native_vector_u16& b, const native_vector_u16& c) {
  native_vector_i48 r = a;
  IVP_ADDWUANX16U(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i24 halide_xtensa_widen_add_i24(const native_vector_i24& a, const native_vector_i8& b) {
  native_vector_i24 r = a;
  IVP_ADDWA2NX8(r, b, native_vector_i8(0));
  return r;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_sat_narrow_i24x_with_shift_i8(const native_vector_i24& a, int shift) {
  return IVP_PACKVRNR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_i24x_with_shift_u8(const native_vector_i24& a, int shift) {
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKVRNR2NX24(a, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i16_x2 halide_xtensa_narrow_i24_with_shift_i16(const native_vector_i24& a, int shift) {
    native_vector_i16 even = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_0(a, shift));
    native_vector_i16 odd = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_1(a, shift));
    native_vector_i16_x2 r;
    IVP_DSELNX16I(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_1);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_narrow_i24_with_shift_i8(const native_vector_i24& a, int shift) {
  return IVP_PACKVR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i32_x2 halide_xtensa_narrow_i48_with_shift_i32(const native_vector_i48& a, int shift) {
    native_vector_i32 even = IVP_PACKVRNRNX48_0(a, shift);
    native_vector_i32 odd = IVP_PACKVRNRNX48_1(a, shift);
    native_vector_i32_x2 r;
    IVP_DSELN_2X32I(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_2);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_u32_x2 halide_xtensa_narrow_i48_with_shift_u32(const native_vector_i48& a, int shift) {
    native_vector_u32 even = IVP_PACKVRNRNX48_0(a, shift);
    native_vector_u32 odd = IVP_PACKVRNRNX48_1(a, shift);
    native_vector_u32_x2 r;
    IVP_DSELN_2X32UI(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_2);
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_narrow_i48_with_shift_u16(const native_vector_i48& a, int shift) {
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(a, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_narrow_with_shift_i16(const native_vector_i32_x2& a, int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_narrow_with_shift_u16(const native_vector_i32_x2& a, int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_narrow_high_i32(const native_vector_i64& a) {
  return IVP_PACKHN_2X64W(a);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_sat_narrow_shift_i32(const native_vector_i64& a, int shift) {
  return IVP_PACKVN_2X64W(a, shift);
}



HALIDE_ALWAYS_INLINE int32_t halide_xtensa_full_reduce_add_u8_to_i32(const native_vector_u8& a) {
    return xb_int16U_rtor_uint16(IVP_RADDU2NX8(a));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_lerp_i16(const native_vector_i16& a, const native_vector_i16& b, uint16_t w) {
  // TODO(vksnk): Halide lerp actually uses full range, but it's not clear from the documentation
  // if we can pass unsigned type to IVP_MULPN16XR16, so just to be extra careful reduce it to 14-bit
  // for now.
  uint32_t w32 = ((uint32_t(w)) >> 0);
  uint32_t alphaMalpha = ((65536 - w32) << 16) | w32;
  xb_vecNx48 output = IVP_MULSUPN16XR16(a, b, alphaMalpha);
  IVP_DECNEGWNX48(output);
  return IVP_PACKVRNX48(output, 16);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_i8>(const native_vector_i8& src) {
  xb_vec2Nx24 wide = src * native_vector_i8(1);
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                        IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_vector_u8>(const native_vector_u8& src) {
  xb_vec2Nx24 wide = src * native_vector_u8(1);
  return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                        IVP_CVT16U2NX24L(wide), IVP_CVT16U2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_u8>(const native_vector_u8& src) {
  xb_vec2Nx24 wide = src * native_vector_u8(1);
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                        IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_i24>(const native_vector_i24& wide) {
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
                        IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_vector_i16_x2>(const native_vector_i16_x2& src) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_i16_x2>(const native_vector_i16_x2& src) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_vector_i32_x4>(const native_vector_i32_x4& src) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
  IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
  return IVP_PACKL2NX24(wide);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i8 convert<native_vector_i8, native_mask_i8>(const native_mask_i8& src) {
  return IVP_MOV2NX8T(native_vector_i8(1), native_vector_i8(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_mask_i8>(const native_mask_i8& src) {
  return IVP_MOV2NX8UT(native_vector_u8(1), native_vector_u8(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_i32_x4>(const native_vector_i32_x4& src) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
  IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_u16_x2>(const native_vector_u16_x2& src) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(src.native_vector[1], src.native_vector[0]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_mask_i16>(const native_mask_i16& src) {
  return IVP_MOVNX16T(native_vector_i16(1), native_vector_i16(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_mask_i8>(const native_mask_i8& src) {
  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector,
            convert<native_vector_i16, native_mask_i16>(IVP_EXTRACTBL2N(src)),
            convert<native_vector_i16, native_mask_i16>(IVP_EXTRACTBH2N(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_i32_x2>(const native_vector_i32_x2& src) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32(src.native_vector[1]),
                      IVP_MOVNX16_FROMN_2X32(src.native_vector[0]),
                      IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i48 convert<native_vector_i48, native_vector_i32_x2>(const native_vector_i32_x2& src) {
  return IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i48 convert<native_vector_i48, native_vector_u32_x2>(const native_vector_u32_x2& src) {
  return IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_u32_x2>(const native_vector_u32_x2& src) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32U(src.native_vector[1]),
                      IVP_MOVNX16_FROMN_2X32U(src.native_vector[0]),
                      IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_i32_x4>(const native_vector_i32_x4& src) {
  xb_vecNx48 wide0 = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  xb_vecNx48 wide1 = IVP_CVT48SNX32(src.native_vector[3], src.native_vector[2]);

  return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, IVP_PACKLNX48(wide0), IVP_PACKLNX48(wide1));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_i32_x2>(const native_vector_i32_x2& src) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32(src.native_vector[1]),
                       IVP_MOVNX16_FROMN_2X32(src.native_vector[0]),
                       IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_mask_i16>(const native_mask_i16& src) {
  return IVP_MOVNX16UT(native_vector_u16(1), native_vector_u16(0), src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_mask_i8>(const native_mask_i8& src) {
  return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
            convert<native_vector_u16, native_mask_i16>(IVP_EXTRACTBL2N(src)),
            convert<native_vector_u16, native_mask_i16>(IVP_EXTRACTBH2N(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_u32_x2>(const native_vector_u32_x2& src) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(src.native_vector[1]),
                       IVP_MOVNX16_FROMN_2X32U(src.native_vector[0]),
                       IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32 convert<native_vector_u32, native_vector_i64>(const native_vector_i64& src) {
  return IVP_PACKLN_2X64W(src);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32 convert<native_vector_i32, native_mask_i32>(const native_mask_i32& src) {
  xb_vecN_2x32v r = 0;
  IVP_INJBIN_2X32(r, src, 0);
  return r;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_u8>(const native_vector_u8& src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                                      IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x4 convert<native_vector_u32_x4, native_vector_u8>(const native_vector_u8& src) {
    xb_vec2Nx24 wide = src * native_vector_u8(1);
    return native_vector_u32_x4(native_vector_u32_x4::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                                      IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_i24>(const native_vector_i24& src) {
    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, IVP_CVT32S2NX24LL(src), IVP_CVT32S2NX24LH(src),
                                                      IVP_CVT32S2NX24HL(src), IVP_CVT32S2NX24HH(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_i16>(const native_vector_i16& src) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src);
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                      IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x4 convert<native_vector_i32_x4, native_vector_i16_x2>(const native_vector_i16_x2& src) {
    auto r0 = convert<native_vector_i32_x2, native_vector_i16>(src.native_vector[0]);
    auto r1 = convert<native_vector_i32_x2, native_vector_i16>(src.native_vector[1]);

    return native_vector_i32_x4(native_vector_i32_x4::from_native_vector, r0.native_vector[0], r0.native_vector[1],
                                                      r1.native_vector[0], r1.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_u16>(const native_vector_u16& src) {
  return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                    IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO)),
                    IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_u32_x2>(const native_vector_u32_x2& src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_i32_x2>(const native_vector_i32_x2& src) {
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16_x2 convert<native_vector_u16_x2, native_vector_i16_x2>(const native_vector_i16_x2& src) {
    return native_vector_u16_x2(native_vector_u16_x2::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_i48>(const native_vector_i48& src) {
    return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                                IVP_CVT32SNX48L(src),
                                IVP_CVT32SNX48H(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_u16>(const native_vector_u16& src) {
    xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, xb_vecNx16U_rtor_xb_vecNx16(src));
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                        xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LL(wide)),
                        xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LH(wide)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u32_x2 convert<native_vector_u32_x2, native_vector_i48>(const native_vector_i48& src) {
    return native_vector_u32_x2(native_vector_u32_x2::from_native_vector,
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src)),
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16_x2 convert<native_vector_i16_x2, native_vector_u16_x2>(const native_vector_u16_x2& src) {
    return native_vector_i16_x2(native_vector_i16_x2::from_native_vector, src.native_vector[0], src.native_vector[1]);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32 convert<native_vector_f32, native_vector_i32>(const native_vector_i32& src) {
  return IVP_FLOATN_2X32(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_i32_x2>(const native_vector_i32_x2& src) {
  return native_vector_f32_x2(native_vector_f32_x2::from_native_vector,
                  convert<native_vector_f32, native_vector_i32>(src.native_vector[0]),
                  convert<native_vector_f32, native_vector_i32>(src.native_vector[1]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_i16>(const native_vector_i16& src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_i16>(src);
    return convert<native_vector_f32_x2, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_u16>(const native_vector_u16& src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_u16>(src);
    return convert<native_vector_f32_x2, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32 convert<native_vector_i32, native_vector_f32>(const native_vector_f32& src) {
  return IVP_TRUNCN_2XF32(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_f32_x2>(const native_vector_f32_x2& src) {
  return native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[0]),
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[1]));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f32_x2 convert<native_vector_f32_x2, native_vector_f16>(const native_vector_f16& src) {
    native_vector_f32_x2 output;

    IVP_DSELN_2XF32I(
      output.native_vector[1],
      output.native_vector[0],
      IVP_CVTF32NXF16_1(src),
      IVP_CVTF32NXF16_0(src),
      IVP_DSELI_INTERLEAVE_2);

    return output;
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_f32_x2>(const native_vector_f32_x2& src) {
    return IVP_SELNXF16I(
      IVP_CVTF16N_2XF32_0(src.native_vector[1]),
      IVP_CVTF16N_2XF32_0(src.native_vector[0]),
      IVP_SELI_EXTRACT_1_OF_2_OFF_0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_i32_x2>(const native_vector_i32_x2& src) {
    return convert<native_vector_f16, native_vector_f32_x2>(
      native_vector_f32_x2(
        native_vector_f32_x2::from_native_vector,
        IVP_FLOATN_2X32(src.native_vector[0], 0),
        IVP_FLOATN_2X32(src.native_vector[1], 0)));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i32_x2 convert<native_vector_i32_x2, native_vector_f16>(const native_vector_f16& src) {
    native_vector_f32_x2 tmp = convert<native_vector_f32_x2, native_vector_f16>(src);
    return convert<native_vector_i32_x2, native_vector_f32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_f32_x2>(const native_vector_f32_x2& src) {
    native_vector_i32_x2 tmp = convert<native_vector_i32_x2, native_vector_f32_x2>(src);
    return convert<native_vector_i16, native_vector_i32_x2>(tmp);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_i16>(const native_vector_i16& src) {
    return IVP_FLOAT16NX16(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_i16 convert<native_vector_i16, native_vector_f16>(const native_vector_f16& src) {
    return IVP_TRUNC16NXF16(src, 0);
}

template<>
HALIDE_ALWAYS_INLINE native_vector_f16 convert<native_vector_f16, native_vector_u16>(const native_vector_u16& src) {
    return convert<native_vector_f16, native_vector_i16>(xb_vecNx16U_rtor_xb_vecNx16(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u16 convert<native_vector_u16, native_vector_f16>(const native_vector_f16& src) {
    return xb_vecNx16U_rtor_xb_vecNx16(convert<native_vector_i16, native_vector_f16>(src));
}

template<>
HALIDE_ALWAYS_INLINE native_vector_u8 convert<native_vector_u8, native_vector_f32_x4>(const native_vector_f32_x4& src) {
    native_vector_i32_x4 tmp(native_vector_i32_x4::from_native_vector,
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[0]),
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[1]),
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[2]),
                  convert<native_vector_i32, native_vector_f32>(src.native_vector[3]));
    return convert<native_vector_u8, native_vector_i32_x4>(tmp);
}

HALIDE_ALWAYS_INLINE native_mask_i32 halide_xtensa_slice_to_native(const native_mask_i16& src, int index, int native_lanes, int total_lanes) {
  return (index == 0)?IVP_EXTRACTBLN(src):IVP_EXTRACTBHN(src);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_i16_low_i32(const native_vector_i16& src) {
    const native_vector_i32 m = native_vector_i32(1U << (16 - 1));
    native_vector_i32 x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(native_vector_i16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
    native_vector_i32 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_i16_high_i32(const native_vector_i16& src) {
    const native_vector_i32 m = native_vector_i32(1U << (16 - 1));
    native_vector_i32 x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(native_vector_i16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
    native_vector_i32 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_u16_low_i32(const native_vector_u16& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_convert_u16_high_i32(const native_vector_u16& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_u16_low_u32(const native_vector_u16& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_u16_high_u32(const native_vector_u16& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(native_vector_u16(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_i32_u16(const native_vector_i32& src0, const native_vector_i32& src1) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src1, src0);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_convert_concat_i16_to_i8(const native_vector_i16& a, const native_vector_i16& b) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(b, a);
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_u8(const native_vector_i16_x2& a) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRU2NX24(wide, 0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_i16(const native_vector_i32_x2& a) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNX48(wide, 0);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_sat_narrow_with_rounding_shift_i8(const native_vector_i16_x2& a, uint32_t shift) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVR2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_sat_narrow_with_rounding_shift_u8(const native_vector_i16_x2& a, uint32_t shift) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRU2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_narrow_with_rounding_shift_i16(const native_vector_i32_x2& a, uint32_t shift) {
  xb_vecNx48 wide = convert<native_vector_i48, native_vector_i32_x2>(a);
  // Add rounding factor.
  const uint16_t half_shift_1 = (shift - 1) >> 1;
  const uint16_t half_shift_2 = (shift - 1) - half_shift_1;
  native_vector_u16 v1 = IVP_SLLNX16U(1, half_shift_1);
  native_vector_u16 v2 = IVP_SLLNX16U(1, half_shift_2);
  IVP_MULUUANX16(wide, v1, v2);
  return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_with_rounding_shift_i16(const native_vector_i32_x2& a, uint32_t shift) {
  xb_vecNx48 wide = convert<native_vector_i48, native_vector_i32_x2>(a);
  return IVP_PACKVRNX48(wide, shift);
}

// TODO(vksnk): this is pretty inefficient.
HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_sat_narrow_with_signed_rounding_shift_i16(const native_vector_i32_x2& a, int32_t shift) {
  if (shift >= 0) {
    return halide_xtensa_sat_narrow_with_rounding_shift_i16(a, (uint32_t)shift);
  }

  return halide_xtensa_sat_narrow_i16(
            native_vector_i32_x2(native_vector_i32_x2::from_native_vector,
                        IVP_SLAN_2X32(a.native_vector[0], -shift),
                        IVP_SLAN_2X32(a.native_vector[1], -shift)));
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_sat_narrow_with_rounding_shift_i32(const native_vector_i64& a, uint32_t shift) {
  return IVP_PACKVRN_2X64W(a, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_rounding_mul_shift_right_i16(const native_vector_i16& a, const native_vector_i16& b, uint16_t shift) {
  xb_vecNx48 wide = a * b;
  return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_rounding_shift_right_i16(const native_vector_i16& a, uint32_t shift) {
  xb_vecNx48 wide = a * (native_vector_i16)1;
  return IVP_PACKVRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_i32 halide_xtensa_rounding_shift_right_i32(const native_vector_i32& a, uint32_t shift) {
  xb_vecN_2x64w wide = a * (native_vector_i32)1;
  return IVP_PACKVRN_2X64W(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_rounding_shift_right_u32(const native_vector_u32& a, uint32_t shift) {
  xb_vecN_2x64w wide = IVP_MULUUN_2X16X32_0((native_vector_u16)1, a);
  return IVP_PACKVRN_2X64W(wide, shift);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_convert_concat_i16_to_u8(const native_vector_i16& a, const native_vector_i16& b) {
  return IVP_SEL2NX8UI(IVP_MOV2NX8_FROMNX16(b), IVP_MOV2NX8_FROMNX16(a), IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i8 halide_xtensa_convert_concat_u16_to_i8(const native_vector_u16& a, const native_vector_u16& b) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(xb_vecNx16U_rtor_xb_vecNx16(b), xb_vecNx16U_rtor_xb_vecNx16(a));
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE native_vector_u8 halide_xtensa_convert_concat_u16_to_u8(const native_vector_u16& a, const native_vector_u16& b) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(xb_vecNx16U_rtor_xb_vecNx16(b), xb_vecNx16U_rtor_xb_vecNx16(a));
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_i8_low_i16(const native_vector_i8& src, int native_lanes, int total_lines) {
    const native_vector_i16 m = native_vector_i16(1U << (8 - 1));
    native_vector_i16 x =  IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
    native_vector_i16 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_i8_high_i16(const native_vector_i8& src, int native_lanes, int total_lines) {
    const native_vector_i16 m = native_vector_i16(1U << (8 - 1));
    native_vector_i16 x =  IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(native_vector_i8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
    native_vector_i16 r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_u8_low_i16(const native_vector_u8& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_u8_high_i16(const native_vector_u8& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_u8_low_u16(const native_vector_u8& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_u8_high_u16(const native_vector_u8& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(native_vector_u8(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_concat_i32_to_i16(const native_vector_i32& a, const native_vector_i32& b) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_concat_i32_to_u16(const native_vector_i32& a, const native_vector_i32& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_i16 halide_xtensa_convert_concat_u32_to_i16(const native_vector_u32& a, const native_vector_u32& b) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_concat_u32_to_u16(const native_vector_u32& a, const native_vector_u32& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE native_vector_u16 halide_xtensa_convert_concat_u32_to_u16_zzz(const native_vector_u32& a, const native_vector_u32& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_i48_low_u32(const native_vector_i48& src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src));
}

HALIDE_ALWAYS_INLINE native_vector_u32 halide_xtensa_convert_i48_high_u32(const native_vector_i48& src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src));
}

HALIDE_ALWAYS_INLINE native_mask_i16 halide_xtensa_concat_from_native(const native_mask_i32& a, const native_mask_i32& b) {
        return IVP_JOINBN_2(b, a);
}

HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_concat_from_native(const native_mask_i16& a, const native_mask_i16& b) {
        return IVP_JOINBN(b, a);
}

HALIDE_ALWAYS_INLINE native_mask_i8 halide_xtensa_concat_from_native(const native_mask_i32& a, const native_mask_i32& b, const native_mask_i32& c, const native_mask_i32& d) {
    return halide_xtensa_concat_from_native(halide_xtensa_concat_from_native(a, b), halide_xtensa_concat_from_native(c, d));
}

HALIDE_ALWAYS_INLINE native_vector_f32_x2 halide_xtensa_concat_from_native(const native_vector_f32& a, const native_vector_f32& b) {
    return native_vector_f32_x2(native_vector_f32_x2::from_native_vector, a, b);
}

template <typename VectorType, typename OffsetType, typename BaseType, int Lanes, bool IsTCM>
HALIDE_ALWAYS_INLINE VectorType gather_load(const void *base, const OffsetType& offset) {
    BaseType __attribute__((aligned(XCHAL_VISION_SIMD8))) tmp[Lanes];
    int offsets[Lanes];
    store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);
    for (int i = 0; i < Lanes; i++) {
        tmp[i] = ((const BaseType*)base)[offsets[i]];
    }

    return *((VectorType *)tmp);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i8 gather_load<native_vector_i8, native_vector_i32_x4, int8_t, VECTOR_WIDTH_U8, true>(const void *base, const native_vector_i32_x4& offset) {
  auto addresses1 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[0], offset.native_vector[1]);
  auto output1 = IVP_GATHERDNX8S(
    IVP_GATHERANX8S(
      (const int8_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(addresses1)
    )
  );

  auto addresses2 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[2], offset.native_vector[3]);
  auto output2 = IVP_GATHERDNX8S(
    IVP_GATHERANX8S(
      (const int8_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(addresses2)
    )
  );

  // NOTE(aelphy): the intrinsic for gathering 8-bit elements extends them to 16-bit, and the conversion back to 8-bit is needed
  return convert<native_vector_i8, native_vector_i16_x2>(native_vector_i16_x2(native_vector_i16_x2::from_native_vector, output1, output2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u8 gather_load<native_vector_u8, native_vector_i32_x4, uint8_t, VECTOR_WIDTH_U8, true>(const void *base, const native_vector_i32_x4& offset) {
  auto addresses1 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[0], offset.native_vector[1]);
  auto output1 = IVP_GATHERDNX8U(
    IVP_GATHERANX8U(
      (const uint8_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(addresses1)
    )
  );

  auto addresses2 = native_vector_i32_x2(native_vector_i32_x2::from_native_vector, offset.native_vector[2], offset.native_vector[3]);
  auto output2 = IVP_GATHERDNX8U(
    IVP_GATHERANX8U(
      (const uint8_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(addresses2)
    )
  );

  // NOTE(aelphy): the intrinsic for gathering 8-bit elements extends them to 16-bit, and the conversion back to 8-bit is needed
  return convert<native_vector_u8, native_vector_u16_x2>(native_vector_u16_x2(native_vector_u16_x2::from_native_vector, output1, output2));
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i16 gather_load<native_vector_i16, native_vector_i32_x2, int16_t, VECTOR_WIDTH_U16, true>(const void *base, const native_vector_i32_x2& offset) {
  // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
  return IVP_GATHERDNX16(
    IVP_GATHERANX16(
      (const int16_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(offset) << 1
    )
  );
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u16 gather_load<native_vector_u16, native_vector_i32_x2, uint16_t, VECTOR_WIDTH_U16, true>(const void *base, const native_vector_i32_x2& offset) {
  // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
  return IVP_GATHERDNX16U(
    IVP_GATHERANX16U(
      (const uint16_t*) base,
      convert<native_vector_u16, native_vector_i32_x2>(offset) << 1
    )
  );
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_i32 gather_load<native_vector_i32, native_vector_i32, int32_t, VECTOR_WIDTH_I32, true>(const void *base, const native_vector_i32& offset) {
  // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
  return IVP_GATHERDN_2X32(
    IVP_GATHERAN_2X32(
      (const int32_t*) base,
      xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2
    )
  );
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_u32 gather_load<native_vector_u32, native_vector_i32, uint32_t, VECTOR_WIDTH_I32, true>(const void *base, const native_vector_i32& offset) {
  // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
  return IVP_GATHERDN_2X32U(
    IVP_GATHERAN_2X32U(
      (const uint32_t*) base,
      xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2
    )
  );
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED native_vector_f32 gather_load<native_vector_f32, native_vector_i32, float, VECTOR_WIDTH_F32, true>(const void *base, const native_vector_i32& offset) {
  // NOTE(aelphy): the shift is needed because offests are expected to be in bytes
  return IVP_GATHERDN_2XF32(
    IVP_GATHERAN_2XF32(
      (const float*) base,
      xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(offset) << 2
    )
  );
}

)INLINE_CODE";

        // Fix: on at least one config (our arm32 buildbot running gcc 5.4),
        // emitting this long text string was regularly garbled in a predictable
        // pattern; flushing the stream before or after heals it. Since C++
        // codegen is rarely on a compilation critical path, we'll just band-aid
        // it in this way.
        stream << std::flush;
        stream << native_typedef_decl;
        stream << std::flush;

        std::set<Type> native_vector_types = {
            Type(Type::Int, 8, target.natural_vector_size<int8_t>()),
            Type(Type::UInt, 8, target.natural_vector_size<uint8_t>()),
            Type(Type::Int, 16, target.natural_vector_size<int16_t>()),
            Type(Type::UInt, 16, target.natural_vector_size<uint16_t>()),
            Type(Type::Int, 32, target.natural_vector_size<int32_t>()),
            Type(Type::UInt, 32, target.natural_vector_size<uint32_t>()),
            Type(Type::Int, 24, target.natural_vector_size<int8_t>()),
            Type(Type::UInt, 24, target.natural_vector_size<uint8_t>()),
            Type(Type::Int, 48, target.natural_vector_size<int16_t>()),
            Type(Type::UInt, 48, target.natural_vector_size<uint16_t>()),
            Type(Type::Int, 64, target.natural_vector_size<int32_t>()),
            Type(Type::Float, 16, target.natural_vector_size<float16_t>()),
            Type(Type::Float, 32, target.natural_vector_size<float>()),
        };

        std::set<Type> predefined_vectors = {
            Int(8, 4),
            UInt(8, 4),
            UInt(8, 8),
            Float(16, 16)};

        std::set<Type> multiple_of_native_types;
        for (const auto &type : vector_types) {
            if (predefined_vectors.count(type) > 0) {
                continue;
            }
            for (const auto &native_vector : native_vector_types) {
                if ((native_vector.code() == type.code()) && (native_vector.bits() == type.bits()) && (type.lanes() > native_vector.lanes()) && (type.lanes() % native_vector.lanes() == 0)) {
                    stream << "using " << print_type(type) << " = MultipleOfNativeVector<" << print_type(native_vector) << ", " << type.lanes() / native_vector.lanes() << ">;\n";
                    multiple_of_native_types.insert(type);
                    break;
                }
            }
        }

        std::set<Type> filtered_vector_types;
        for (const auto &t : vector_types) {
            if ((native_vector_types.count(t) > 0) || (predefined_vectors.count(t) > 0) || (multiple_of_native_types.count(t) > 0)) {
                continue;
            }
            filtered_vector_types.insert(t);
        }

        CodeGen_C::add_vector_typedefs(filtered_vector_types);
    }
}

string CodeGen_Xtensa::print_assignment(Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        const char *const_flag = output_kind == CPlusPlusImplementation ? "const " : "";
        if (t.is_handle()) {
            // Don't print void *, which might lose useful type information. just use auto.
            stream << get_indent() << "auto * __restrict ";
        } else {
            stream << get_indent() << print_type(t, AppendSpace);
        }
        stream << const_flag << id << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

std::string CodeGen_Xtensa::print_type(Type t, AppendSpaceIfNeeded space_option) {
    if (t.bits() == 1 && t.is_vector()) {
        return "uint1x" + std::to_string(t.lanes()) + "_t" + (space_option == AppendSpace ? " " : "");
    } else if (t.is_float() && t.is_vector()) {
        return "float" + std::to_string(t.bits()) + "x" + std::to_string(t.lanes()) + "_t" + (space_option == AppendSpace ? " " : "");
    }
    return CodeGen_C::print_type(t, space_option);
}

void CodeGen_Xtensa::visit(const IntImm *op) {
    if (op->type.is_int() && (op->type.bits() <= 32)) {
        id = std::to_string(op->value);
    } else {
        static const char *const suffixes[3] = {
            "ll",  // PlainC
            "l",   // OpenCL
            "",    // HLSL
        };
        print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + suffixes[(int)integer_suffix_style] + ")");
    }
}
void CodeGen_Xtensa::visit(const Mul *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        print_expr(Call::make(op->type, Call::shift_left, {op->a, Expr(bits)}, Call::PureIntrinsic));
    } else {
        if (is_native_xtensa_vector<int16_t>(op->type, target)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_MULNX16PACKL(" + sa + ", " + sb + ")");
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_PACKLN_2X64W(IVP_MULN_2X32(" + sa + ", " + sb + "))");
        } else {
            visit_binop(op->type, op->a, op->b, "*");
        }
    }
}

string CodeGen_Xtensa::print_xtensa_call(const Call *op) {
    ostringstream rhs;

    vector<string> args(op->args.size());

    if (op->name == "halide_xtensa_widening_load") {
        internal_assert(op->args.size() == 3);
        const Variable *src = op->args[0].as<Variable>();
        internal_assert(src != nullptr);
        args[0] = print_name(src->name);
        args[1] = print_expr(op->args[1]);
        // We are only using args[2] argument to get the type of the load.

        rhs << "widening_load<" << print_type(op->type) << ", " << print_type(op->args[2].type()) << ">(" << args[0] << ", " << args[1] << ")";
        return rhs.str();
    }

    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }

    if (op->name == "halide_xtensa_pad_to_native" || op->name == "halide_xtensa_slice_from_padded") {
        internal_assert(op->args.size() == 2);
        // TODO(vksnk): bools are tricky, because they are bitmasks, so need to be
        // handled differently.
        const int bytes_in_vector = target.natural_vector_size<uint8_t>();
        if (op->type.is_bool()) {
            internal_assert((op->type.lanes() == bytes_in_vector && op->args[0].type().lanes() == bytes_in_vector / 2) || (op->type.lanes() == bytes_in_vector / 2 && op->args[0].type().lanes() == bytes_in_vector / 4) || (op->type.lanes() == bytes_in_vector && op->args[0].type().lanes() == bytes_in_vector / 4)) << Expr(op);
        }
        rhs << op->name << "<" << print_type(op->args[0].type()) << ", "
            << print_type(op->type) << ", " << print_type(op->type.element_of())
            << ", " << op->args[0].type().lanes() << ", " << op->type.lanes()
            << ">(" << args[0] << ", " << args[1] << ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_slice_to_native" && !op->type.is_bool()) {
        Type native_vector_type = get_native_xtensa_vector(op->type, target);
        int vector_count = op->type.lanes() / native_vector_type.lanes();

        if (vector_count == 1) {
            rhs << args[0] << ".native_vector[" << args[1] << "]";
        } else {
            rhs << print_type(op->type) << "(" << print_type(op->type) << "::from_native_vector, ";
            std::vector<std::string> native_vectors;
            for (int ix = 0; ix < vector_count; ix++) {
                native_vectors.push_back(args[0] + ".native_vector[" + args[1] + " * " + std::to_string(vector_count) + " + " + std::to_string(ix) + "]");
            }
            rhs << with_commas(native_vectors) << ")";
        }
        return rhs.str();
    }

    if (op->name == "halide_xtensa_concat_from_native" && !op->type.is_bool()) {
        rhs << print_type(op->type) << "(" << print_type(op->type) << "::from_native_vector, " << with_commas(args) << ")";
        return rhs.str();
    }

    if ((op->name.find("halide_xtensa_slice_right") == 0) || (op->name.find("halide_xtensa_slice_left") == 0)) {
        string intrinsic_name;
        string shift_define;
        string direction = (op->name.find("halide_xtensa_slice_right") == 0) ? "RIGHT_" : "LEFT_";
        if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            intrinsic_name = "IVP_SEL2NX8I";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            intrinsic_name = "IVP_SEL2NX8UI";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
            intrinsic_name = "IVP_SELNX16I";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
            intrinsic_name = "IVP_SELNX16UI";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            intrinsic_name = "IVP_SELN_2X32I";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
            intrinsic_name = "IVP_SELN_2X32UI";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            intrinsic_name = "IVP_SELNXF16I";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<float>(op->type, target)) {
            intrinsic_name = "IVP_SELN_2XF32I";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else {
            internal_assert(false) << "Unsupported type for slicing";
        }

        rhs << intrinsic_name << "(" << args[0] << ".native_vector[1], " << args[0] << ".native_vector[0], " << shift_define << direction << args[1] << ")";

        return rhs.str();
    }
    // absd needs extra cast to uint*
    if (op->name == "halide_xtensa_absd_i16") {
        rhs << "xb_vecNx16_rtor_xb_vecNx16U(IVP_ABSSUBNX16(" << args[0] + ", " + args[1] + "))";
        return rhs.str();
    } else if (op->name == "halide_xtensa_narrow_i48_with_shift_u16") {
        rhs << "xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(" << args[0] + ", " + args[1] + "))";
        return rhs.str();
    } else if (op->name == "halide_xtensa_convert_i48_low_u32") {
        rhs << "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(" << args[0] + "))";
        return rhs.str();
    } else if (op->name == "halide_xtensa_convert_i48_high_u32") {
        rhs << "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(" << args[0] + "))";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_extract_i32" || op->name == "halide_xtensa_extract_u32") {
        rhs << "IVP_EXTRN_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" << args[0] + ")), " + args[1] + ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_dual_extract_i32") {
        rhs << "IVP_DEXTRPRN_2X32("
            << "IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" + args[0] + ")), "
            << "IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(" + args[1] + ")), "
            << args[2] + ", " + args[3] + ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_dynamic_shuffle") {
        if (is_native_vector_type(op->args[0].type(), target) && is_native_vector_type(op->args[1].type(), target)) {
            rhs << "IVP_SHFL" << intrinsic_suffix_for_type(op->type) << "("
                << args[0] + ", " + args[1] + ")";
            return rhs.str();
        }
    }

    string op_name = op->name;
    std::map<string, string> op_name_to_intrinsic = {
        {"halide_xtensa_abs_i8", "IVP_ABS2NX8"},
        {"halide_xtensa_abs_i16", "IVP_ABSNX16"},
        {"halide_xtensa_abs_i32", "IVP_ABSN_2X32"},
        {"halide_xtensa_abs_f32", "IVP_ABSN_2XF32"},
        {"halide_xtensa_sat_add_i16", "IVP_ADDSNX16"},
        {"halide_xtensa_sat_sub_i16", "IVP_SUBSNX16"},
        {"halide_xtensa_avg_i8", "IVP_AVG2NX8"},
        {"halide_xtensa_avg_u8", "IVP_AVGU2NX8"},
        {"halide_xtensa_avg_i16", "IVP_AVGNX16"},
        {"halide_xtensa_avg_u16", "IVP_AVGUNX16"},
        {"halide_xtensa_avg_round_i8", "IVP_AVGR2NX8"},
        {"halide_xtensa_avg_round_u8", "IVP_AVGRU2NX8U"},
        {"halide_xtensa_avg_round_i16", "IVP_AVGRNX16"},
        {"halide_xtensa_avg_round_u16", "IVP_AVGRUNX16U"},
        {"halide_xtensa_widen_mul_i48", "IVP_MULNX16"},
        {"halide_xtensa_widen_mul_u48", "IVP_MULUUNX16"},
        {"halide_xtensa_mul_i32", "IVP_MULN_2X32"},
        {"halide_xtensa_widen_mul_ui48", "IVP_MULUSNX16"},
        {"halide_xtensa_widen_pair_mul_u48", "IVP_MULUUPNX16"},
        {"halide_xtensa_convert_i48_low_i32", "IVP_CVT32SNX48L"},
        {"halide_xtensa_convert_i48_high_i32", "IVP_CVT32SNX48H"},
        {"halide_xtensa_convert_i48_low_u32", "IVP_CVT32UNX48L"},
        {"halide_xtensa_convert_i48_high_u32", "IVP_CVT32UNX48H"},
        {"halide_xtensa_narrow_i48_with_shift_i16", "IVP_PACKVRNRNX48"},
        {"halide_xtensa_sat_narrow_i48_with_shift_i16", "IVP_PACKVRNX48"},
        {"halide_xtensa_full_reduce_add_i8", "IVP_RADD2NX8"},
        {"halide_xtensa_full_reduce_add_i16", "IVP_RADDNX16"},
        {"halide_xtensa_full_reduce_add_i32", "IVP_RADDN_2X32"},

        {"halide_xtensa_full_reduce_min_u8", "IVP_RMINU2NX8U"},
        {"halide_xtensa_full_reduce_min_u16", "IVP_RMINUNX16U"},
        {"halide_xtensa_full_reduce_min_u32", "IVP_RMINUN_2X32U"},
        {"halide_xtensa_full_reduce_min_i8", "IVP_RMIN2NX8"},
        {"halide_xtensa_full_reduce_min_i16", "IVP_RMINNX16"},
        {"halide_xtensa_full_reduce_min_i32", "IVP_RMINN_2X32"},

        {"halide_xtensa_full_reduce_max_u8", "IVP_RMAXU2NX8U"},
        {"halide_xtensa_full_reduce_max_u16", "IVP_RMAXUNX16U"},
        {"halide_xtensa_full_reduce_max_u32", "IVP_RMAXUN_2X32U"},
        {"halide_xtensa_full_reduce_max_i8", "IVP_RMAX2NX8"},
        {"halide_xtensa_full_reduce_max_i16", "IVP_RMAXNX16"},
        {"halide_xtensa_full_reduce_max_i32", "IVP_RMAXN_2X32"},

        {"halide_xtensa_sat_left_shift_i16", "IVP_SLSNX16"},
        {"halide_xtensa_sat_left_shift_i32", "IVP_SLSN_2X32"},
    };

    if (op_name_to_intrinsic.count(op_name) > 0) {
        op_name = op_name_to_intrinsic[op_name];
    }

    rhs << op_name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_Xtensa::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        print_expr(Call::make(op->type, Call::shift_right, {op->a, Expr(bits)}, Call::PureIntrinsic));
    } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
        ostringstream rhs;
        rhs << "IVP_DIVNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    } else if (is_native_xtensa_vector<float>(op->type, target)) {
        ostringstream rhs;
        rhs << "IVP_DIVN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    } else {
        string sa = print_expr(op->a);
        string sb = print_expr(op->b);
        // Just cast to clang vector types and use division defined on them.
        if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            print_assignment(op->type, "(common_uint8x64_t)" + sa + " / (common_uint8x64_t)" + sb);
        } else if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            print_assignment(op->type, "(common_int8x64_t)" + sa + " / (common_int8x64_t)" + sb);
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            print_assignment(op->type, "(common_int32x16_t)" + sa + " / (common_int32x16_t)" + sb);
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
            print_assignment(op->type, "(common_uint32x16_t)" + sa + " / (common_uint32x16_t)" + sb);
        } else {
            print_assignment(op->type, sa + " / " + sb);
        }
    }
}

void CodeGen_Xtensa::visit(const Mod *op) {
    if (is_native_xtensa_vector<int32_t>(op->type, target)) {
        string sa = print_expr(op->a);
        string sb = print_expr(op->b);
        print_assignment(op->type, "(common_int32x16_t)" + sa + " % (common_int32x16_t)" + sb);
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const Max *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_max<" + print_type(op->type) + ">", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            rhs << "IVP_MAX2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            rhs << "IVP_MAXU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
            rhs << "IVP_MAXNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
            rhs << "IVP_MAXUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            rhs << "IVP_MAXN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
            rhs << "IVP_MAXUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_MAXNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_MAXN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else {
            rhs << print_type(op->type) << "::max(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Xtensa::visit(const Min *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_min<" + print_type(op->type) + ">", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            rhs << "IVP_MIN2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            rhs << "IVP_MINU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
            rhs << "IVP_MINNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
            rhs << "IVP_MINUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            rhs << "IVP_MINN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
            rhs << "IVP_MINUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_MINNXF16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_MINN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else {
            rhs << print_type(op->type) << "::min(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Xtensa::visit(const Select *op) {
    ostringstream rhs;
    string type = print_type(op->type);
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);

    if (op->condition.type().is_scalar()) {
        rhs << "(" << type << ")"
            << "(" << cond
            << " ? " << true_val
            << " : " << false_val
            << ")";
    } else {
        if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            rhs << "IVP_MOV2NX8T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            rhs << "IVP_MOV2NX8UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
            rhs << "IVP_MOVNX16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
            rhs << "IVP_MOVNX16UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            rhs << "IVP_MOVN_2X32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
            rhs << "IVP_MOVN_2X32UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_MOVNXF16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_MOVN_2XF32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else {
            rhs << type << "::select(" << cond << ", " << true_val << ", " << false_val << ")";
        }
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Ramp *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);
    int int32_lanes = target.natural_vector_size<int32_t>();
    if (is_const_one(op->stride)) {
        if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            print_assignment(vector_type, "/* ramp */ int32x" + std::to_string(int32_lanes) + "_t(" + id_base + ") + IVP_SEQN_2X32()");
        } else {
            // If it's wide enough split it here into concat of smaller ramps.
            if (op->type.is_int() && (op->type.bits() == 32) && (op->type.lanes() % int32_lanes == 0) && (op->type.lanes() / int32_lanes > 4)) {
                int split_to = op->type.lanes() / int32_lanes;

                std::vector<Expr> concat_args;
                for (int ix = 0; ix < split_to; ix++) {
                    Expr r = Ramp::make(op->base + op->stride * (int32_lanes * ix), op->stride, int32_lanes);
                    concat_args.push_back(std::move(r));
                }
                Expr concat = Call::make(op->type,
                                         "halide_xtensa_concat_from_native",
                                         concat_args, Call::PureExtern);

                concat.accept(this);
            } else {
                print_assignment(vector_type, "dense_ramp<" + print_type(vector_type) + ">(" + id_base + ")");
            }
        }
    } else {
        if (is_native_xtensa_vector<int32_t>(op->type, target)) {
            print_assignment(vector_type, "/* ramp */ int32x" + std::to_string(int32_lanes) + "_t(" + id_base + ") + IVP_PACKLN_2X64W(IVP_SEQN_2X32() * int32x16_t(" + id_stride + "))");
        } else if ((op->type.lanes() == 32 || op->type.lanes() == 64 || op->type.lanes() == 128) && op->type.is_int_or_uint() && op->type.bits() == 32) {
            print_assignment(vector_type, "ramp<" + print_type(vector_type) + ">(" + id_base + ", " + id_stride + ")");
        } else {
            print_assignment(vector_type, print_type(vector_type) + "_ops::ramp(" + id_base + ", " + id_stride + ")");
        }
    }
}

void CodeGen_Xtensa::visit(const Broadcast *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string rhs;
    if (op->type.is_int() && ((op->type.bits() == 24) || (op->type.bits() == 48)) && is_const(op->value)) {
        // Assigning a constant to wide vector is tricky.
        if (is_const_zero(op->value)) {
            if (op->type.bits() == 24) {
                rhs = "IVP_ZERO2NX24()";
            } else if (op->type.bits() == 48) {
                rhs = "IVP_ZERONX48()";
            }
        } else {
            rhs = std::to_string(op->value.as<IntImm>()->value);
        }
    } else if (op->type.is_int_or_uint() && op->type.bits() == 8 && ((op->type.lanes() == 4) || (op->type.lanes() == 8))) {
        string id_value = print_expr(op->value);
        rhs = "broadcast<" + print_type(op->type) + ", " + print_type(op->value.type()) + ">(" + id_value + ")";
    } else {
        string id_value = print_expr(op->value);

        if (is_native_vector_type(op->type, target)) {
            // TODO(vsknk): why it this extra cast to scalar is needed?
            rhs = print_type(vector_type) + "((" + print_type(op->type.with_lanes(1)) + ")" + id_value + ")";
        } else if (op->lanes > 1) {
            if (op->type.is_bool()) {
                // TODO(vksnk): figure out how to broadcast bool.
                if (op->type.lanes() == 16) {
                    rhs = id_value + "? (int32x16_t(1) == int32x16_t(1)) : (int32x16_t(1) == int32x16_t(0))";
                } else if (op->type.lanes() == 32) {
                    rhs = id_value + "? (int16x32_t(1) == int16x32_t(1)) : (int16x32_t(1) == int16x32_t(0))";
                } else if (op->type.lanes() == 64) {
                    rhs = id_value + "? (int8x64_t(1) == int8x64_t(1)) : (int8x64_t(1) == int8x64_t(0))";
                }
            } else {
                rhs = id_value;
            }
        } else {
            rhs = id_value;
        }
    }

    print_assignment(vector_type, rhs);
}

void CodeGen_Xtensa::visit(const LE *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LE2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LEU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LENX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LEUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LEN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LEUN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OLENXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OLEN_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const LT *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LT2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LTU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LTNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LTUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LTN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_LTUN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OLTNXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OLTN_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const GE *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GE2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GEU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GENX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GEUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GEN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GEUN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OGENXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OGEN_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const GT *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GT2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GTU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GTNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GTUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GTN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_GTUN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OGTNXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OGTN_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const Or *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (op->a.type().is_bool() && op->type.is_vector()) {
        if (op->a.type().lanes() == 16) {
            print_assignment(op->type, "IVP_ORBN_2(" + sa + ", " + sb + ")");
        } else if (op->a.type().lanes() == 32) {
            print_assignment(op->type, "IVP_ORBN(" + sa + ", " + sb + ")");
        } else if (op->a.type().lanes() == 64) {
            print_assignment(op->type, "IVP_ORB2N(" + sa + ", " + sb + ")");
        } else {
            internal_assert(false) << "Unhandled boolean type in the || op\n";
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const EQ *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQ2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQ2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_EQN_2X32U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float16_t>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OEQNXF16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<float>(op->a.type(), target)) {
        print_assignment(op->type, "IVP_OEQN_2XF32(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const Load *op) {
    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.
    ostringstream rhs;

    Type t = op->type;
    string name = print_name(op->name);

    // If we're loading a contiguous ramp into a vector, just load the vector
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (!is_const_one(op->predicate)) {
        const Call *pred = op->predicate.as<Call>();
        if (pred && (pred->name == "clamped_dense_ramp") && dense_ramp_base.defined()) {
            internal_assert(t.is_vector());
            // The number of elements is difference between upper bound and base of the ramp
            // plus one (because the predicate is <=).
            Expr count = simplify(pred->args[1] - pred->args[0] + 1);
            string id_ramp_base = print_expr(dense_ramp_base);
            string id_count = print_expr(count);
            rhs << "load_variable"
                << "<" << print_type(t) << ", "
                << print_type(t.element_of()) << ", " << t.lanes()
                << ">(" << name << ", " << id_ramp_base << ", " << id_count << ")";
        } else {
            string id_index = print_expr(op->index);
            string id_predicate = print_expr(op->predicate);
            rhs << "load_predicated<" << print_type(t) << ", "
                << print_type(op->index.type()) << ", "
                << print_type(op->predicate.type()) << ", "
                << print_type(t.element_of()) << ", " << t.lanes()
                << ">(" << name << ", " << id_index << ", " << id_predicate << ")";
        }
    } else if (dense_ramp_base.defined()) {
        internal_assert(t.is_vector());
        std::string op_name;
        const int bytes_in_vector = target.natural_vector_size<uint8_t>();
        int native_lanes = (bytes_in_vector / op->type.element_of().bytes());
        if (op->type.element_of().bytes() == 3) {
            native_lanes = bytes_in_vector;
        }
        if (op->type.element_of().bytes() == 6) {
            native_lanes = bytes_in_vector / 2;
        }
        bool is_aligned_load = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_load = is_aligned_load && (op->param.host_alignment() % bytes_in_vector == 0);
        }
        if (is_aligned_load) {
            op_name = "aligned_load";
        } else {
            op_name = "load";
        }
        string id_ramp_base = print_expr(dense_ramp_base);
        rhs << op_name << "<" << print_type(t) << ", "
            << print_type(t.element_of()) << ", " << t.lanes()
            << ">(" << name << ", " << id_ramp_base << ")";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(t.is_vector());
        // NOTE(vksnk): strided_load may be a good idea, but needs more work.
        // const Ramp* maybe_ramp = op->index.as<Ramp>();
        // if (maybe_ramp && is_const(maybe_ramp->stride)) {
        //     string id_index_base = print_expr(maybe_ramp->base);
        //     string id_index_stride = print_expr(maybe_ramp->stride);
        //     rhs << print_type(t) + "_strided_load(" << name << ", "
        //         << id_index_base << ", " << id_index_stride << ")";
        // } else {
        string id_index = print_expr(op->index);
        // Is not allocated on the heap and is not a buffer
        bool is_tcm = !(heap_allocations.contains(name) || external_buffers.count(op->name) > 0);

        rhs << "gather_load<" << print_type(t) << ", "
            << print_type(Int(32, t.lanes())) << ", "
            << print_type(t.element_of()) << ", "
            << t.lanes() << ", " << is_tcm << ">("
            << name << ", " << id_index << ")";
        // }
    } else {
        string id_index = print_expr(op->index);
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type.element_of() == t.element_of());
        if (type_cast_needed) {
            rhs << "((const " << print_type(t.element_of()) << " *)" << name << ")";
        } else {
            rhs << name;
        }
        rhs << "[" << id_index << "]";
    }
    print_assignment(t, rhs.str());
}

void CodeGen_Xtensa::visit(const Store *op) {
    Type t = op->value.type();

    if (inside_atomic_mutex_node) {
        user_assert(t.is_scalar())
            << "The vectorized atomic operation for the store" << op->name
            << " is lowered into a mutex lock, which does not support vectorization.\n";
    }

    // Issue atomic store if we are in the designated producer.
    if (emit_atomic_stores) {
        stream << "#if defined(_OPENMP)\n";
        stream << "#pragma omp atomic\n";
        stream << "#else\n";
        stream << "#error \"Atomic stores in the C backend are only supported in compilers that support OpenMP.\"\n";
        stream << "#endif\n";
    }

    bool is_narrowing = false;
    bool is_sat_narrowing = false;
    Expr value = op->value;
    if (const Cast *cast = value.as<Cast>()) {
        if (cast->value.type().is_vector() && cast->type.is_int_or_uint() && cast->value.type().is_int_or_uint() && (cast->value.type().bits() == value.type().bits() * 2)) {
            is_narrowing = true;
            value = cast->value;
        }
    }
    if (const Call *call = value.as<Call>()) {
        // TODO: more checks for this one are needed.
        if (call->name == "halide_xtensa_slice_from_padded") {
            if (const Cast *cast = call->args[0].as<Cast>()) {
                if (cast->value.type().is_vector() && cast->type.is_int_or_uint() && cast->value.type().is_int_or_uint() && (cast->value.type().bits() == value.type().bits() * 2)) {
                    if (const Call *inner_call = cast->value.as<Call>()) {
                        if (inner_call->name == "halide_xtensa_pad_to_native") {
                            is_narrowing = true;
                            value = inner_call->args[0];
                        }
                    }
                }
            }
        }
        // TODO(vksnk): disabled for now, because corresponding implementation
        // is missing.
        // if (call->name.find("halide_xtensa_sat_narrow_i") == 0) {
        //     is_sat_narrowing = true;
        //     value = call->args[0];
        // }
    }

    string id_value = print_expr(value);
    string name = print_name(op->name);

    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.

    // If we're writing a contiguous ramp, just store the vector.
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);

    if (!is_const_one(op->predicate)) {
        const Call *pred = op->predicate.as<Call>();
        if (pred && (pred->name == "clamped_dense_ramp") && dense_ramp_base.defined()) {
            // The number of elements is difference between upper bound and base of the ramp
            // plus one (because the predicate is <=).
            Expr count = simplify(pred->args[1] - pred->args[0] + 1);
            internal_assert(op->value.type().is_vector());
            string id_ramp_base = print_expr(dense_ramp_base);
            string id_count = print_expr(count);
            string op_name = "store_variable";
            if (is_narrowing) {
                op_name = op_name + "_narrowing";
            }
            if (is_sat_narrowing) {
                op_name = op_name + "_narrowing_sat";
            }
            stream << get_indent() << op_name << "<";
            if (is_narrowing) {
                stream << print_type(value.type());
            } else {
                stream << print_type(t);
            }
            stream << ", " << print_type(t.element_of()) << ", " << t.lanes()
                   << ">(" << id_value << ", " << name << ", " << id_ramp_base << ", " << id_count << ");\n";
        } else {
            string id_index = print_expr(op->index);
            string id_predicate = print_expr(op->predicate);
            stream << get_indent() << "store_predicated<" << print_type(t) << ", "
                   << print_type(op->index.type()) << ", "
                   << print_type(op->predicate.type()) << ", "
                   << print_type(t.element_of()) << ", " << t.lanes()
                   << ">(" << id_value << ", " << name << ", " << id_index << ", " << id_predicate << ");\n";
        }
    } else if (dense_ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string op_name;
        const int bytes_in_vector = target.natural_vector_size<uint8_t>();
        int native_lanes = (bytes_in_vector / op->value.type().element_of().bytes());
        if (op->value.type().element_of().bytes() == 3) {
            native_lanes = bytes_in_vector;
        }
        if (op->value.type().element_of().bytes() == 6) {
            native_lanes = bytes_in_vector / 2;
        }

        bool is_aligned_store = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_store = is_aligned_store && (op->param.host_alignment() % bytes_in_vector == 0);
        }

        if (is_aligned_store) {
            op_name = "aligned_store";
        } else {
            op_name = "store";
        }

        if (is_narrowing) {
            op_name = op_name + "_narrowing";
        }
        if (is_sat_narrowing) {
            op_name = op_name + "_narrowing_sat";
        }

        string id_ramp_base = print_expr(dense_ramp_base);
        stream << get_indent() << op_name << "<";
        if (is_narrowing) {
            stream << print_type(value.type());
        } else {
            stream << print_type(t);
        }
        stream << ", " << print_type(t.element_of()) << ", " << t.lanes()
               << ">(" << id_value << ", " << name << ", " << id_ramp_base << ");\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());
        string id_index = print_expr(op->index);
        stream << get_indent() << "store_scatter<" << print_type(t) << ", "
               << print_type(op->index.type()) << ", "
               << print_type(t.element_of()) << ", " << t.lanes()
               << ">(" << id_value << ", " << name << ", " << id_index << ");\n";
    } else {
        bool type_cast_needed =
            t.is_handle() ||
            !allocations.contains(op->name) ||
            allocations.get(op->name).type != t;

        string id_index = print_expr(op->index);
        stream << get_indent();
        if (type_cast_needed) {
            stream << "((" << print_type(t) << " *)" << name << ")";
        } else {
            stream << name;
        }
        stream << "[" << id_index << "] = " << id_value << ";\n";
    }
    cache.clear();
}

void CodeGen_Xtensa::visit(const Call *op) {
    ostringstream rhs;

    // Handle intrinsics first
    if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        const int64_t *bits = as_const_int(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type, target) && bits) {
            rhs << "IVP_SLLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int8_t>(op->type, target) && bits) {
            rhs << "IVP_SLLI2NX8(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target) && bits) {
            rhs << "IVP_SLLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target) && bits) {
            rhs << "IVP_SLLINX16(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target) && bits) {
            rhs << "IVP_SLLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target) && bits) {
            rhs << "IVP_SLLIN_2X32(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
                rhs << "IVP_SLLNX16U(" << a0 << ", xb_vecNx16U_rtor_xb_vecNx16(" << a1 << "))";
            } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
                rhs << "IVP_SLANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
                rhs << "IVP_SLLN_2X32U(" << a0 << ", xb_vecN_2x32Uv_rtor_xb_vecN_2x32v( " << a1 << "))";
            } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
                rhs << "IVP_SLAN_2X32(" << a0 << ", " << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    if (op->type.is_vector()) {
                        rhs << "scalarize_binary<" << print_type(op->type) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << op->type.lanes() << ">(&halide_shift_left, "
                            << print_expr(op->args[0])
                            << ", " << print_expr(op->args[1]) << ")";

                    } else {
                        string a0 = print_expr(op->args[0]);
                        string a1 = print_expr(op->args[1]);
                        rhs << a0 << " << " << a1;
                    }
                } else {
                    rhs << print_expr(lower_signed_shift_left(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        const int64_t *bits = as_const_int(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type, target) && bits) {
            rhs << "IVP_SRLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int8_t>(op->type, target) && bits) {
            rhs << "IVP_SRAI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type, target) && bits) {
            rhs << "IVP_SRAINX16(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type, target) && bits) {
            rhs << "IVP_SRLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target) && bits) {
            rhs << "IVP_SRAIN_2X32(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target) && bits) {
            rhs << "IVP_SRLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint16_t>(op->type, target)) {
                rhs << "IVP_SRLNX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int16_t>(op->type, target)) {
                rhs << "IVP_SRANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type, target)) {
                rhs << "IVP_SRLN_2X32(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int32_t>(op->type, target)) {
                rhs << "IVP_SRAN_2X32(" << a0 << ", (" << print_type(op->type) << ")" << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    if (op->type.is_vector()) {
                        rhs << "scalarize_binary<" << print_type(op->type) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << print_type(op->type.with_lanes(1)) << ", "
                            << op->type.lanes() << ">(&halide_shift_right, "
                            << print_expr(op->args[0])
                            << ", " << print_expr(op->args[1]) << ")";
                    } else {
                        string a0 = print_expr(op->args[0]);
                        string a1 = print_expr(op->args[1]);
                        rhs << a0 << " >> " << a1;
                    }
                } else {
                    rhs << print_expr(lower_signed_shift_right(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::count_leading_zeros)) {
        internal_assert(op->args.size() == 1);
        if (is_native_xtensa_vector<int16_t>(op->type, target) || is_native_xtensa_vector<uint16_t>(op->type, target)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUNX16(" : "xb_vecNx16_rtor_xb_vecNx16U(IVP_NSAUNX16U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (is_native_xtensa_vector<int32_t>(op->type, target) || is_native_xtensa_vector<uint32_t>(op->type, target)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUN_2X32(" : "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_NSAUN_2X32U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (op->args[0].type().is_vector()) {
            // Xtensa doesn't have 8-bit intrinsics for count_leading_zeros.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_count_leading_zeros is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_count_leading_zeros, " << print_expr(op->args[0]) << ")";
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        if (is_native_xtensa_vector<int8_t>(op->type, target)) {
            rhs << "IVP_POPC2NX8(" << print_expr(op->args[0]) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type, target)) {
            rhs << "IVP_POPC2NX8U(" << print_expr(op->args[0]) << ")";
        } else if (op->type.is_vector()) {
            // Xtensa only has popcount intrinsics for 8-bit vector types.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_popcount is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_popcount, " << print_expr(op->args[0]) << ")";
        } else {
            CodeGen_C::visit(op);
            return;
        }
    } else if (op->is_intrinsic(Call::count_trailing_zeros)) {
        internal_assert(op->args.size() == 1);
        if (op->type.is_vector()) {
            // Xtensa doesn't have intrinsics for count_trailing_zeros.
            rhs << "scalarize_unary<" << print_type(op->type) << ", "
                << print_type(op->type.with_lanes(1)) << ", "
                // return type of halide_count_trailing_zeros is always int.
                << "int, "
                << op->type.lanes() << ">(&halide_count_trailing_zeros, " << print_expr(op->args[0]) << ")";
        } else {
            CodeGen_C::visit(op);
            return;
        }
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_error << "Prefetch is not supported by Xtensa backend." << Expr(op) << "\n";
    } else if (op->name == "sqrt" || op->name == "sqrt_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_FSQRTN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_FSQRTNXF16(" << a0 << ")";
        } else {
            rhs << "sqrtf(" << a0 << ")";
        }
    } else if (op->name == "round" || op->name == "round_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_FIRINTN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_FIRINTNXF16(" << a0 << ")";
        } else {
            rhs << "nearbyint(" << a0 << ")";
        }
    } else if (op->name == "floor" || op->name == "floor_f32") {
        string a0 = print_expr(op->args[0]);
        if (is_native_xtensa_vector<float>(op->type, target)) {
            rhs << "IVP_FIFLOORN_2XF32(" << a0 << ")";
        } else if (is_native_xtensa_vector<float16_t>(op->type, target)) {
            rhs << "IVP_FIFLOORNXF16(" << a0 << ")";
        } else {
            rhs << "floor_f32(" << a0 << ")";
        }
    } else if (op->name.find("halide_xtensa_") == 0) {
        rhs << print_xtensa_call(op);
    } else {
        CodeGen_C::visit(op);
        return;
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Cast *op) {
    const Type &t = op->type;
    const Expr &e = op->value;
    string value = print_expr(e);
    string type = print_type(t);
    if ((is_native_xtensa_vector<int8_t>(t, target) || is_native_xtensa_vector<uint8_t>(t, target)) && (is_native_xtensa_vector<int8_t>(e.type(), target) || is_native_xtensa_vector<uint8_t>(e.type(), target))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vec2Nx8_rtor_xb_vec2Nx8U(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vec2Nx8U_rtor_xb_vec2Nx8(" + value + ")");
        }
    } else if ((is_native_xtensa_vector<int16_t>(t, target) || is_native_xtensa_vector<uint16_t>(t, target)) && (is_native_xtensa_vector<int16_t>(e.type(), target) || is_native_xtensa_vector<uint16_t>(e.type(), target))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecNx16_rtor_xb_vecNx16U(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecNx16U_rtor_xb_vecNx16(" + value + ")");
        }
    } else if ((is_native_xtensa_vector<int32_t>(t, target) || is_native_xtensa_vector<uint32_t>(t, target)) && (is_native_xtensa_vector<int32_t>(e.type(), target) || is_native_xtensa_vector<uint32_t>(e.type(), target))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(" + value + ")");
        }
    } else if (is_native_xtensa_vector<int64_t>(e.type(), target) && is_native_xtensa_vector<int32_t>(t, target)) {
        id = print_assignment(t, "IVP_PACKLN_2X64W(" + value + ")");
    } else if (t.is_vector() &&
               t.lanes() == e.type().lanes() &&
               t != e.type()) {
        id = print_assignment(t, "convert<" + type + "," + print_type(e.type()) + ">(" + value + ")");
    } else {
        id = print_assignment(t, "(" + type + ")(" + value + ")");
    }
}

void CodeGen_Xtensa::visit(const Reinterpret *op) {
    if (is_native_vector_type(op->type, target) && is_native_vector_type(op->value.type(), target)) {
        string op_name = "";
        if (is_native_xtensa_vector<int32_t>(op->type, target) && is_native_xtensa_vector<uint32_t>(op->value.type(), target)) {
            op_name = "xb_vecN_2x32Uv_rtor_xb_vecN_2x32v";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target) && is_native_xtensa_vector<int32_t>(op->value.type(), target)) {
            op_name = "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv";
        } else if (is_native_xtensa_vector<uint32_t>(op->type, target) && is_native_xtensa_vector<float>(op->value.type(), target)) {
            op_name = "IVP_MOVN_2X32_FROMN_2XF32";
        } else if (is_native_xtensa_vector<float>(op->type, target) && is_native_xtensa_vector<uint32_t>(op->value.type(), target)) {
            op_name = "IVP_MOVN_2XF32_FROMN_2X32";
        }
        if (!op_name.empty()) {
            string value = print_expr(op->value);
            id = print_assignment(op->type, op_name + "(" + value + ")");
            return;
        }
    }
    CodeGen_C::visit(op);
}

void CodeGen_Xtensa::visit(const For *op) {
    current_loop_level++;
    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    if (op->for_type == ForType::Parallel) {
        stream << get_indent() << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == ForType::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }

#if POOR_MANS_PROFILING_LOOP_LEVEL > 0
    std::string n = op->name;
    for (auto &c : n) {
        if (c == '$' || c == '.') {
            c = '_';
        }
    }
    if (current_loop_level <= POOR_MANS_PROFILING_LOOP_LEVEL) {
        open_scope();
        stream << get_indent() << "const int cycles_start_" << n << " = GetCycleCount();\n";
    }
#endif

    stream << get_indent() << "for (int "
           << print_name(op->name)
           << " = " << id_min
           << "; "
           << print_name(op->name)
           << " < " << id_min
           << " + " << id_extent
           << "; "
           << print_name(op->name)
           << "++)\n";
    open_scope();

    op->body.accept(this);

    close_scope("for " + print_name(op->name));
#if POOR_MANS_PROFILING_LOOP_LEVEL > 0
    if (current_loop_level <= POOR_MANS_PROFILING_LOOP_LEVEL) {
        stream << get_indent() << "const int cycles_stop_" << n << " = GetCycleCount();\n";
        stream << get_indent() << "const int cycles_tot_" << n << " = cycles_stop_" << n << " - cycles_start_" << n << ";\n";
        stream << get_indent() << "printf(\"@" << current_loop_level << ": " << op->name << ": %d\\n\", cycles_tot_" << n << ");\n";
        close_scope("profiler" + print_name(op->name));
    }
#endif
    current_loop_level--;
}

void CodeGen_Xtensa::visit(const Shuffle *op) {
    internal_assert(!op->vectors.empty());
    for (size_t i = 1; i < op->vectors.size(); i++) {
        internal_assert(op->vectors[0].type() == op->vectors[i].type());
    }
    internal_assert(op->type.lanes() == (int)op->indices.size());
    const int max_index = (int)(op->vectors[0].type().lanes() * op->vectors.size());
    for (int i : op->indices) {
        internal_assert(i >= -1 && i < max_index);
    }

    // Generate intrinsics for the interleave op.
    int vector_size_in_bytes = target.natural_vector_size<uint8_t>();
    if (op->is_interleave() && (is_native_vector_type(op->vectors[0].type(), target) || is_double_native_vector_type(op->vectors[0].type(), target) || (op->vectors[0].type().is_bool() && op->vectors[0].type().lanes() == vector_size_in_bytes))) {
        string type_suffix = suffix_for_type(op->type);

        Expr call = Call::make(op->type, "halide_xtensa_interleave" + type_suffix,
                               op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    if (op->is_slice() && (op->slice_stride() == 1) &&
        (is_native_xtensa_vector<int8_t>(op->type, target) || is_native_xtensa_vector<uint8_t>(op->type, target) || is_native_xtensa_vector<int16_t>(op->type, target) || is_native_xtensa_vector<uint16_t>(op->type, target) || is_native_xtensa_vector<int32_t>(op->type, target) || is_native_xtensa_vector<uint32_t>(op->type, target) || is_native_xtensa_vector<float>(op->type, target) || is_native_xtensa_vector<float16_t>(op->type, target))) {
        string type_suffix = suffix_for_type(op->type);
        string function_name = "halide_xtensa_slice";
        int slice_begin = op->slice_begin();
        if (op->slice_begin() < 5 || (op->slice_begin() == 6) || (op->slice_begin() == 8)) {
            function_name += "_right";
        }
        if ((op->type.lanes() - op->slice_begin() < 5) && (op->type.lanes() > op->slice_begin())) {
            function_name += "_left";
            slice_begin = op->type.lanes() - op->slice_begin();
        }
        Expr call = Call::make(op->type, function_name + type_suffix,
                               {op->vectors[0], slice_begin}, Call::PureExtern);
        call.accept(this);
        return;
    }

    if (op->vectors.size() == 1) {
        if (op->is_slice() && (op->slice_begin() < 2) && (op->slice_stride() == 2) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 2)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_deinterleave") + ((op->slice_begin() == 0) ? "_even" : "_odd");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
        if (op->is_slice() && (op->slice_begin() >= 0 && op->slice_begin() < 4) && (op->slice_stride() == 4) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 4)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_extract_" + std::to_string(op->slice_begin()) + "_of_4");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
    }

    if (op->is_concat() && is_native_vector_type(op->vectors[0].type(), target)) {
        Expr call = Call::make(op->type, "halide_xtensa_concat_from_native", op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    std::vector<string> vecs;
    for (const Expr &v : op->vectors) {
        vecs.push_back(print_expr(v));
    }
    string src = vecs[0];
    Type src_type = op->vectors[0].type();
    if (op->vectors.size() > 1) {
        ostringstream rhs;
        rhs << "concat<"
            << print_type(op->type) << ", "
            << print_type(op->vectors[0].type()) << ", "
            << print_type(op->type.element_of()) << ", "
            << op->type.lanes() << ", "
            << op->vectors[0].type().lanes()
            << ">(" << with_commas(vecs) << ")";
        src = print_assignment(op->type, rhs.str());
        src_type = src_type.with_lanes(src_type.lanes() * op->vectors.size());
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        rhs << src << "[" << op->indices[0] << "]";
    } else if (op->is_concat()) {
        // Do nothing if it's just concat.
        return;
    } else if (op->type.bits() == 24 && op->vectors[0].type().lanes() == 128 && op->type.is_int()) {
        if (op->is_slice() && op->slice_begin() == 0 && op->slice_stride() == 1 && op->indices.size() == 64) {
            rhs << src << ".native_vector[0]";
        }
        if (op->is_slice() && op->slice_begin() == 64 &&
            op->slice_stride() == 1 && op->indices.size() == 64) {
            rhs << src << ".native_vector[1]";
        }
    } else {
        string indices_name = unique_name('_');
        stream << get_indent() << "const int32_t " << indices_name << "[" << op->indices.size() << "] = { " << with_commas(op->indices) << " };\n";
        rhs << "shuffle"
            << "<"
            << print_type(src_type) << ", "
            << print_type(op->type) << ", "
            << print_type(op->type.element_of()) << ", " << src_type.lanes()
            << ", " << op->type.lanes()
            << ">(" << src << ", " << indices_name << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_Xtensa::visit(const Allocate *op) {
    open_scope();

    string op_name = print_name(op->name);
    string op_type = print_type(op->type, AppendSpace);

    // For sizes less than 8k, do a stack allocation
    bool on_stack = false;
    int32_t constant_size;
    string size_id;
    Type size_id_type;

    if (op->new_expr.defined()) {
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
        heap_allocations.push(op->name);
        stream << op_type << "*" << op_name << " = (" << print_expr(op->new_expr) << ");\n";
    } else {
        constant_size = op->constant_allocation_size();
        if (constant_size > 0) {
            int64_t stack_bytes = constant_size * op->type.bytes();

            if (stack_bytes > ((int64_t(1) << 31) - 1)) {
                user_error << "Total size for allocation "
                           << op->name << " is constant but exceeds 2^31 - 1.\n";
            } else {
                size_id_type = Int(32);
                size_id = print_expr(make_const(size_id_type, constant_size));

                if (op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register) {
                    on_stack = true;
                }
            }
        } else {
            // Check that the allocation is not scalar (if it were scalar
            // it would have constant size).
            internal_assert(!op->extents.empty());

            size_id = print_assignment(Int(64), print_expr(op->extents[0]));
            size_id_type = Int(64);

            for (size_t i = 1; i < op->extents.size(); i++) {
                // Make the code a little less cluttered for two-dimensional case
                string new_size_id_rhs;
                string next_extent = print_expr(op->extents[i]);
                if (i > 1) {
                    new_size_id_rhs = "(" + size_id + " > ((int64_t(1) << 31) - 1)) ? " + size_id + " : (" + size_id + " * " + next_extent + ")";
                } else {
                    new_size_id_rhs = size_id + " * " + next_extent;
                }
                size_id = print_assignment(Int(64), new_size_id_rhs);
            }
        }

        // Check the condition to see if this allocation should actually be created.
        // If the allocation is on the stack, the only condition we can respect is
        // unconditional false (otherwise a non-constant-sized array declaration
        // will be generated).
        if (!on_stack || is_const_zero(op->condition)) {
            Expr conditional_size = Select::make(op->condition,
                                                 Variable::make(size_id_type, size_id),
                                                 make_const(size_id_type, 0));
            conditional_size = simplify(conditional_size);
            size_id = print_assignment(Int(64), print_expr(conditional_size));
        }

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        stream << get_indent() << op_type;

        if (on_stack) {
            stream << "__attribute__((aligned(XCHAL_VISION_SIMD8))) " << op_name
                   << "[" << size_id << "];\n";
        } else if (op->memory_type == MemoryType::VTCM) {
            stream << "*"
                   << "__attribute__((aligned(XCHAL_VISION_SIMD8))) "
                   << " __restrict "
                   << op_name
                   << " = ("
                   << op_type
                   << " *)halide_tcm_malloc(_ucon, sizeof("
                   << op_type
                   << ")*" << size_id << ");\n";
        } else {
            stream << "*"
                   << "__attribute__((aligned(XCHAL_VISION_SIMD8)))  "
                   << " __restrict "
                   << op_name
                   << " = ("
                   << op_type
                   << " *)halide_malloc(_ucon, sizeof("
                   << op_type
                   << ")*" << size_id << ");\n";
            heap_allocations.push(op->name);
        }
    }

    if (!on_stack) {
        ostringstream check;
        if (is_const_zero(op->condition)) {
            // Assertion always succeeds here, since allocation is never used
            check << print_expr(const_true());
        } else {
            // Assert that the allocation worked....
            check << "((" << op_name << " != nullptr) || (" << size_id << " == 0))";
            if (!is_const_one(op->condition)) {
                // ...but if the condition is false, it's OK for the new_expr to be null.
                string op_condition = print_assignment(Bool(), print_expr(op->condition));
                check << " || (!" << op_condition << ")";
            }
        }
        create_assertion(check.str(), Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern));

        string free_function = op->free_function.empty() ?
                                   (op->memory_type != MemoryType::VTCM ? "halide_free" : "halide_tcm_free") :
                                   op->free_function;

        stream << get_indent();
        stream << "HalideFreeHelper " << op_name << "_free(_ucon, "
               << op_name << ", " << free_function << ");\n";
    }

    op->body.accept(this);

    // Free the memory if it was allocated on the heap and there is no matching
    // Free node.
    print_heap_free(op->name);
    if (allocations.contains(op->name)) {
        allocations.pop(op->name);
    }

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_Xtensa::visit(const Let *op) {
    const auto *call = op->value.as<Call>();
    if (call && (call->name == "clamped_dense_ramp")) {
        Expr body = substitute(op->name, call, op->body);
        body.accept(this);
        return;
    }
    return CodeGen_C::visit(op);
}

void CodeGen_Xtensa::visit(const LetStmt *op) {
    const auto *call = op->value.as<Call>();
    if (call && (call->name == "clamped_dense_ramp")) {
        Stmt body = substitute(op->name, call, op->body);
        body.accept(this);
        return;
    }
    return CodeGen_C::visit(op);
}

}  // namespace Internal
}  // namespace Halide

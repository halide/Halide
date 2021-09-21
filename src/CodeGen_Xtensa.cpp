#include "CodeGen_Xtensa.h"

#include <string>

#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Lerp.h"
#include "Simplify.h"
#include "Substitute.h"
#include "XtensaOptimize.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

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
    for (size_t i = 0; i < args.size(); i++) {
        // TODO: check that its type is void *?
        have_user_context |= (args[i].name == "__user_context");
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

    Stmt body = f.body;
    body = match_xtensa_patterns(body);

    // Emit the function prototype
    if (f.linkage == LinkageType::Internal) {
        // If the function isn't public, mark it static.
        stream << "static ";
    }
    stream << "HALIDE_FUNCTION_ATTRS\n";
    stream << "int " << simple_name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            external_buffers.insert(args[i].name);
            stream << "struct halide_buffer_t *"
                   << print_name(args[i].name)
                   << "_buffer";
        } else {
            stream << print_type(args[i].type, AppendSpace)
                   << print_name(args[i].name);
        }

        if (i < args.size() - 1) stream << ", ";
    }

    if (is_header_or_extern_decl()) {
        stream << ");\n";
    } else {
        stream << ") {\n";
        indent += 1;

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
                stream << get_indent() << "halide_unused(_ucon);";
            }

            stream << "ScopedDmaInitializer dma_initializer;\n";
            // stream << "printf(\"" << simple_name << "\\n\");";
            // Emit the body
            print(body);
            // stream << "printf(\"[end]" << simple_name << "\\n\");";

            // Return success.
            stream << get_indent() << "return 0;\n";
        }

        indent -= 1;
        stream << "}\n";
    }

    if (f.linkage == LinkageType::ExternalPlusMetadata) {
        // Emit the argv version
        emit_argv_wrapper(simple_name, args);

        // And also the metadata.
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
    if (!vector_types.empty()) {
        const char *native_typedef_decl = R"INLINE_CODE(


#if defined(__XTENSA__)
#include <xtensa/sim.h>
#include <xtensa/tie/xt_ivpn.h>
#include <xtensa/tie/xt_timer.h>

// This inline function is needed by application to get the cycle count from ISS
inline int GetCycleCount() {
  return XT_RSR_CCOUNT();
}

#endif
#include <xtensa/tie/xt_ivpn.h>

#define HALIDE_MAYBE_UNUSED __attribute__ ((unused))

// NOTE(vksnk): we can use clang native vectors in place of Xtensa
// data types, and while they should be much more convinient, there is
// a slight performance degradation, which needs to be investigated.
// typedef int8_t int8x64_t __attribute__((ext_vector_type(64)));
// typedef uint8_t uint8x64_t __attribute__((ext_vector_type(64)));
// typedef int16_t int16x32_t __attribute__((ext_vector_type(32)));
// typedef uint16_t uint16x32_t __attribute__((ext_vector_type(32)));
// typedef int32_t int32x16_t __attribute__((ext_vector_type(16)));
// typedef uint32_t uint32x16_t __attribute__((ext_vector_type(16)));

typedef int32_t common_int32x16_t __attribute__((ext_vector_type(16)));
typedef uint32_t common_uint32x16_t __attribute__((ext_vector_type(16)));

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
using float32x16_t = xb_vecN_2xf32;
using int8x4_t = xb_int32pr;
using uint8x4_t = xb_int32pr;
using int8x8_t = xb_int64pr;
using uint8x8_t = xb_int64pr;

template <typename NativeVector, int N>
struct MultipleOfNativeVector {
  NativeVector  __attribute__((aligned(64))) native_vector[N];

  MultipleOfNativeVector() {}

  // TODO(vksnk): figure out a better/safer way to construct it.
  enum FromCppVector { from_native_vector };
  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2) {
      static_assert(N == 2, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
  }

  inline MultipleOfNativeVector(FromCppVector, const NativeVector &src1, const NativeVector &src2, const NativeVector &src3, const NativeVector &src4) {
      static_assert(N == 4, "Wrong kind of constructor");
      native_vector[0] = src1;
      native_vector[1] = src2;
      native_vector[2] = src3;
      native_vector[3] = src4;
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

using uint1x256_t = MultipleOfNativeVector<uint1x64_t, 4>;
using int8x128_t = MultipleOfNativeVector<int8x64_t, 2>;
using int8x256_t = MultipleOfNativeVector<int8x64_t, 4>;
using uint8x128_t = MultipleOfNativeVector<uint8x64_t, 2>;
using uint8x192_t = MultipleOfNativeVector<uint8x64_t, 3>;
using uint8x256_t = MultipleOfNativeVector<uint8x64_t, 4>;
using int16x64_t = MultipleOfNativeVector<int16x32_t, 2>;
using uint16x64_t = MultipleOfNativeVector<uint16x32_t, 2>;
using int16x128_t = MultipleOfNativeVector<int16x32_t, 4>;
using uint16x128_t = MultipleOfNativeVector<uint16x32_t, 4>;
using int24x128_t = MultipleOfNativeVector<int24x64_t, 2>;
using int32x32_t = MultipleOfNativeVector<int32x16_t, 2>;
using uint32x32_t = MultipleOfNativeVector<uint32x16_t, 2>;
using int32x64_t = MultipleOfNativeVector<int32x16_t, 4>;
using uint32x64_t = MultipleOfNativeVector<uint32x16_t, 4>;
// TODO(vksnk): this one should be generated automatically, but isn't.
using int32x192_t = MultipleOfNativeVector<int32x16_t, 12>;
using int32x256_t = MultipleOfNativeVector<int32x16_t, 16>;
using int48x64_t = MultipleOfNativeVector<int48x32_t, 2>;
using float32x32_t = MultipleOfNativeVector<float32x16_t, 2>;
using float32x64_t = MultipleOfNativeVector<float32x16_t, 4>;

template <typename ResultType>
HALIDE_ALWAYS_INLINE ResultType ramp(int32_t base, int32_t stride) = delete;

template <typename ResultType>
HALIDE_ALWAYS_INLINE ResultType dense_ramp(int32_t base) = delete;

template<>
HALIDE_ALWAYS_INLINE int32x32_t ramp<int32x32_t>(int32_t base, int32_t stride) {
    int32x16_t one_to_n = IVP_SEQN_2X32();
    int32x16_t base_w = base;
    int32x16_t stride_w = stride;
    int32x16_t lanes_2 = 16;
    return int32x32_t(int32x32_t::from_native_vector, IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
            IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE int32x32_t dense_ramp<int32x32_t>(int32_t base) {
    const int32x16_t base_w = int32x16_t(base) + IVP_SEQN_2X32();
    const int32x16_t lanes_2 = 16;
    return int32x32_t(int32x32_t::from_native_vector, base_w, base_w + lanes_2);
}

template<>
HALIDE_ALWAYS_INLINE int32x64_t ramp<int32x64_t>(int32_t base, int32_t stride) {
    int32x16_t one_to_n = IVP_SEQN_2X32();
    int32x16_t base_w = base;
    int32x16_t stride_w = stride;
    int32x16_t lanes_2 = 16;
    int32x16_t lanes_3 = 32;
    int32x16_t lanes_4 = 48;

    return int32x64_t(int32x64_t::from_native_vector,
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_2 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_3 + one_to_n, stride_w))),
                IVP_ADDN_2X32(base_w, IVP_PACKLN_2X64W(IVP_MULN_2X32(lanes_4 + one_to_n, stride_w))));
}

template<>
HALIDE_ALWAYS_INLINE int32x64_t dense_ramp<int32x64_t>(int32_t base) {
    int32x16_t base_w = IVP_ADDN_2X32(int32x16_t(base), IVP_SEQN_2X32());
    int32x16_t lanes_2 = 16;
    int32x16_t lanes_3 = 32;
    int32x16_t lanes_4 = 48;

    return int32x64_t(int32x64_t::from_native_vector,
                        base_w,
                        IVP_ADDN_2X32(base_w, lanes_2),
                        IVP_ADDN_2X32(base_w, lanes_3),
                        IVP_ADDN_2X32(base_w, lanes_4));
}

template <typename ResultType, typename BaseType>
HALIDE_ALWAYS_INLINE ResultType broadcast(BaseType value) = delete;

template <>
HALIDE_ALWAYS_INLINE uint8x4_t broadcast<uint8x4_t, uint8_t>(uint8_t value) {
    uint8x64_t v = value;
    return IVP_EXTRPRN_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template <>
HALIDE_ALWAYS_INLINE uint8x8_t broadcast<uint8x8_t, uint8_t>(uint8_t value) {
    uint8x64_t v = value;
    return IVP_EXTRPR64N_2X32(IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(v)), 0);
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType aligned_load(const void *base, int32_t offset) {
    return *((const VectorType *)((const BaseType*)base + offset));
}

template <>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t aligned_load<int32x32_t, int32_t, 32>(const void *base, int32_t offset) {
    const int32x16_t * __restrict ptr = ((const int32x16_t *)((const int32_t*)base + offset));
    int32x32_t r;
    r.native_vector[0] = *ptr++;
    r.native_vector[1] = *ptr++;
    return r;
}

template <>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int8x256_t aligned_load<int8x256_t, int8_t, 256>(const void *base, int32_t offset) {
    const int8x64_t * __restrict ptr = ((const int8x64_t *)((const int8_t*)base + offset));
    int8x256_t r;
    r.native_vector[0] = *ptr++;
    r.native_vector[1] = *ptr++;
    r.native_vector[2] = *ptr++;
    r.native_vector[3] = *ptr++;
    return r;
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
HALIDE_ALWAYS_INLINE void store_variable<uint8x64_t, uint8_t, 64>(const uint8x64_t& a, void *base, int32_t offset, int32_t count) {
	valign align;
	xb_vec2Nx8U* __restrict ptr  = (xb_vec2Nx8U*)((uint8_t*)base + offset);
	IVP_SAV2NX8U_XP(a, align, ptr, count);
	IVP_SAPOS2NX8U_FP(align, ptr);
}

template <typename VectorType, typename OffsetType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType gather_load(const void *base, const OffsetType& offset) {
    BaseType __attribute__((aligned(64))) tmp[Lanes];
    int offsets[Lanes];
    store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);
    for (int i = 0; i < Lanes; i++) {
        tmp[i] = ((const BaseType*)base)[offsets[i]];
    }

    return *((VectorType *)tmp);
}

template <typename VectorType, typename OffsetType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_scatter(const VectorType& a, void *base, const OffsetType& offset) {
    BaseType __attribute__((aligned(64))) tmp[Lanes];
    aligned_store<VectorType, BaseType, Lanes>(a, &tmp[0], 0);

    int __attribute__((aligned(64))) offsets[Lanes];
    aligned_store<OffsetType, int32_t, Lanes>(offset, &offsets[0], 0);

    for (int i = 0; i < Lanes; i++) {
        ((BaseType*)base)[offsets[i]] = tmp[i];
    }
}

template <typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE VectorType load_predicated(const void *base, const OffsetType& offset, const PredicateType& predicate) = delete;

template <>
HALIDE_ALWAYS_INLINE uint8x64_t load_predicated<uint8x64_t, int32x64_t, uint1x64_t, uint8_t, 64>(const void *base, const int32x64_t& offset, const uint1x64_t& predicate) {
    int __attribute__((aligned(64))) offsets[64];
    aligned_store<int32x64_t, int32_t, 64>(offset, &offsets[0], 0);
    uint8x64_t vmask = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate);
    uint8_t __attribute__((aligned(64))) mask[64];
    aligned_store<uint8x64_t, uint8_t, 64>(vmask, &mask[0], 0);

    uint8_t __attribute__((aligned(64))) output[64];
    for (int i = 0; i < 64; i++) {
        if (mask[i] == 1) {
            output[i] = ((const uint8_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((uint8x64_t *)output);
}

template <>
HALIDE_ALWAYS_INLINE int32x64_t load_predicated<int32x64_t, int32x64_t, uint1x64_t, int32_t, 64>(const void *base, const int32x64_t& offset, const uint1x64_t& predicate) {
    int __attribute__((aligned(64))) offsets[64];
    aligned_store<int32x64_t, int32_t, 64>(offset, &offsets[0], 0);
    uint8x64_t vmask = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate);
    uint8_t __attribute__((aligned(64))) mask[64];
    aligned_store<uint8x64_t, uint8_t, 64>(vmask, &mask[0], 0);

    int32_t __attribute__((aligned(64))) output[64];
    for (int i = 0; i < 64; i++) {
        if (mask[i] == 1) {
            output[i] = ((const int32_t*)base)[offsets[i]];
        } else {
            output[i] = 0;
        }
    }

    return *((int32x64_t *)output);
}

template <typename VectorType, typename OffsetType, typename PredicateType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_predicated(const VectorType& a, void *base, const OffsetType& offset, const PredicateType& predicate) = delete;

template <>
HALIDE_ALWAYS_INLINE void store_predicated<uint8x64_t, int32x64_t, uint1x64_t, uint8_t, 64>(const uint8x64_t& a, void *base, const int32x64_t& offset, const uint1x64_t& predicate) {
    uint8_t __attribute__((aligned(64))) tmp[64];
    aligned_store<uint8x64_t, uint8_t, 64>(a, &tmp[0], 0);

    int __attribute__((aligned(64))) offsets[64];
    aligned_store<int32x64_t, int32_t, 64>(offset, &offsets[0], 0);

    uint8x64_t vmask = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate);
    uint8_t __attribute__((aligned(64))) mask[64];
    aligned_store<uint8x64_t, uint8_t, 64>(vmask, &mask[0], 0);

    for (int i = 0; i < 64; i++) {
        if (mask[i]) {
            ((uint8_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

template <>
HALIDE_ALWAYS_INLINE void store_predicated<uint8x256_t, int32x256_t, uint1x256_t, uint8_t, 256>(const uint8x256_t& a, void *base, const int32x256_t& offset, const uint1x256_t& predicate) {
    uint8_t __attribute__((aligned(64))) tmp[256];
    aligned_store<uint8x256_t, uint8_t, 256>(a, &tmp[0], 0);

    int __attribute__((aligned(64))) offsets[256];
    aligned_store<int32x256_t, int32_t, 256>(offset, &offsets[0], 0);

    uint8x64_t vmask0 = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate.native_vector[0]);
    uint8x64_t vmask1 = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate.native_vector[1]);
    uint8x64_t vmask2 = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate.native_vector[2]);
    uint8x64_t vmask3 = IVP_MOV2NX8T(uint8x64_t(1), uint8x64_t(1), predicate.native_vector[3]);

    uint8_t __attribute__((aligned(64))) mask[256];
    aligned_store<uint8x256_t, uint8_t, 256>(
        uint8x256_t(uint8x256_t::from_native_vector, vmask0, vmask1, vmask2, vmask3), &mask[0], 0);

    for (int i = 0; i < 256; i++) {
        if (mask[i]) {
            ((uint8_t*)base)[offsets[i]] = tmp[i];
        }
    }
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo shuffle(const VectorTypeFrom& a, const int32_t indices[LanesTo]) {
    BaseType  __attribute__((aligned(64))) tmp1[LanesFrom];
    BaseType  __attribute__((aligned(64))) tmp2[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, &tmp1[0], 0);
    for (int i = 0; i < LanesTo; i++) {
        tmp2[i] = tmp1[indices[i]];
    }

    return *((VectorTypeTo *)tmp2);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b) {
    BaseType  __attribute__((aligned(64))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);

    return *((ResultType *)tmp);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b, const ArgType& c) {
    BaseType  __attribute__((aligned(64))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);

    return *((ResultType *)tmp);
}

template <typename ResultType, typename ArgType, typename BaseType, int LanesResult, int LanesArg>
HALIDE_ALWAYS_INLINE ResultType concat(const ArgType& a, const ArgType& b, const ArgType& c, const ArgType& d) {
    BaseType  __attribute__((aligned(64))) tmp[LanesResult];

    store<ArgType, BaseType, LanesArg>(a, &tmp[0], 0);
    store<ArgType, BaseType, LanesArg>(b, &tmp[0], LanesArg);
    store<ArgType, BaseType, LanesArg>(c, &tmp[0], 2 * LanesArg);
    store<ArgType, BaseType, LanesArg>(d, &tmp[0], 3 * LanesArg);

    return *((ResultType *)tmp);
}

template <>
HALIDE_ALWAYS_INLINE int32x32_t concat<int32x32_t, int32x16_t, int32_t, 32, 16>(const int32x16_t& a, const int32x16_t& b) {
  return int32x32_t(int32x32_t::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE int32x64_t concat<int32x64_t, int32x16_t, int32_t, 64, 16>(const int32x16_t& a, const int32x16_t& b, const int32x16_t& c, const int32x16_t& d) {
  return int32x64_t(int32x64_t::from_native_vector, a, b, c, d);
}

template <>
HALIDE_ALWAYS_INLINE int16x64_t concat<int16x64_t, int16x32_t, int16_t, 64, 32>(const int16x32_t& a, const int16x32_t& b) {
  return int16x64_t(int16x64_t::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE uint16x64_t concat<uint16x64_t, uint16x32_t, uint16_t, 64, 32>(const uint16x32_t& a, const uint16x32_t& b) {
  return uint16x64_t(uint16x64_t::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE uint8x128_t concat<uint8x128_t, uint8x64_t, uint8_t, 128, 64>(const uint8x64_t& a, const uint8x64_t& b) {
  return uint8x128_t(uint8x128_t::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE float32x32_t concat<float32x32_t, float32x16_t, float, 32, 16>(const float32x16_t& a, const float32x16_t& b) {
  return float32x32_t(float32x32_t::from_native_vector, a, b);
}

template <>
HALIDE_ALWAYS_INLINE int24x128_t concat<int24x128_t, int24x64_t, int24_t, 128, 64>(const int24x64_t& a, const int24x64_t& b) {
  return int24x128_t(int24x128_t::from_native_vector, a, b);
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_pad_to_native(const VectorTypeFrom& a, int lanes) {
    BaseType  __attribute__((aligned(64))) tmp[LanesTo];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template <typename VectorTypeFrom, typename VectorTypeTo, typename BaseType, int LanesFrom, int LanesTo>
HALIDE_ALWAYS_INLINE VectorTypeTo halide_xtensa_slice_from_padded(const VectorTypeFrom& a, int lanes) {
    BaseType  __attribute__((aligned(64))) tmp[LanesFrom];
    store<VectorTypeFrom, BaseType, LanesFrom>(a, tmp, 0);
    return load<VectorTypeTo, BaseType, LanesTo>(tmp, 0);
}

template <>
HALIDE_ALWAYS_INLINE uint1x32_t halide_xtensa_pad_to_native<uint1x16_t, uint1x32_t, bool, 16, 32>(const uint1x16_t& a, int lanes) {
    return IVP_JOINBN_2(a, a);
}

template <>
HALIDE_ALWAYS_INLINE uint1x64_t halide_xtensa_pad_to_native<uint1x32_t, uint1x64_t, bool, 32, 64>(const uint1x32_t& a, int lanes) {
    return IVP_JOINBN(a, a);
}

template <>
HALIDE_ALWAYS_INLINE uint1x64_t halide_xtensa_pad_to_native<uint1x16_t, uint1x64_t, bool, 16, 64>(const uint1x16_t& a, int lanes) {
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
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x64_t load<uint8x64_t, uint8_t, 64>(const void *base, int32_t offset) {
    uint8x64_t r;
    const xb_vec2Nx8U*  __restrict ptr = (const xb_vec2Nx8U*)((const uint8_t*)base + offset);
    IVP_L2U2NX8U_XP(r, ptr, 0);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<int8x64_t, int8_t, 64>(const int8x64_t& a, void *base, int32_t offset) {
	valign align;
	xb_vec2Nx8* __restrict ptr  = (xb_vec2Nx8*)((int8_t*)base + offset);
	IVP_SA2NX8_IP(a, align, ptr);
	IVP_SAPOS2NX8_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE void store<uint8x64_t, uint8_t, 64>(const uint8x64_t& a, void *base, int32_t offset) {
	valign align;
	xb_vec2Nx8U* __restrict ptr  = (xb_vec2Nx8U*)((uint8_t*)base + offset);
	IVP_SA2NX8U_IP(a, align, ptr);
	IVP_SAPOS2NX8U_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x32_t load<int16x32_t, int16_t, 32>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8*  __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16_IP(r, align, (const xb_vecNx16*)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<int16x32_t, int16_t, 32>(const int16x32_t& a, void *base, int32_t offset) {
    valign align;
    xb_vecNx16* ptr = (xb_vecNx16*)((int16_t*)base + offset);
    IVP_SANX16_IP(a, align, ptr);
    // Flush alignment register.
    IVP_SAPOSNX16_FP(align, ptr);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint16x32_t load<uint16x32_t, uint16_t, 32>(const void *base, int32_t offset) {
    xb_vecNx16U r;
    const xb_vec2Nx8*  __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX16U_IP(r, align, (const xb_vecNx16U*)ptr8);

    return r;
}

template<>
HALIDE_ALWAYS_INLINE void store<uint16x32_t, uint16_t, 32>(const uint16x32_t& a, void *base, int32_t offset) {
	valign align;
	xb_vecNx16U* ptr  = (xb_vecNx16U*)((uint16_t*)base + offset);
	IVP_SANX16U_IP(a, align, ptr);
	IVP_SAPOSNX16U_FP(align, ptr);
}

// It seems that this is buggy
/*
template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x64_t load<int16x64_t, int16_t, 64>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vecNx16* ptr = (const xb_vecNx16*)((const int16_t*)base + offset);
    IVP_L2UNX16_XP(r1, ptr, 0);
    ptr++;
    IVP_L2UNX16_XP(r2, ptr, 0);
    return int16x64_t(int16x64_t::from_native_vector, r1, r2);
}
*/
template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t load<int32x32_t, int32_t, 32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1;
    const xb_vecN_2x32v* __restrict ptr = (const xb_vecN_2x32v*)((const int32_t*)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    return int32x32_t(int32x32_t::from_native_vector, nv8_0, nv8_1);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x64_t load<int32x64_t, int32_t, 32>(const void *base, int32_t offset) {
    xb_vecN_2x32v nv8_0, nv8_1, nv8_2, nv8_3;
    const xb_vecN_2x32v* __restrict ptr = (const xb_vecN_2x32v*)((const int32_t*)base + offset);
    valign align = IVP_LA_PP((const xb_vec2Nx8 *)ptr);
    IVP_LAN_2X32_IP(nv8_0, align, ptr);
    IVP_LAN_2X32_IP(nv8_1, align, ptr);
    IVP_LAN_2X32_IP(nv8_2, align, ptr);
    IVP_LAN_2X32_IP(nv8_3, align, ptr);
    return int32x64_t(int32x64_t::from_native_vector, nv8_0, nv8_1, nv8_2, nv8_3);
}

template <typename ResultType, typename LoadType>
HALIDE_ALWAYS_INLINE ResultType widening_load(const void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x32_t widening_load<int16x32_t, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r, align, (const xb_vecNx8U*)ptr8);
    return r;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x64_t widening_load<int16x64_t, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U*)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U*)ptr8);

    return int16x64_t(int16x64_t::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint16x64_t widening_load<uint16x64_t, uint8_t>(const void *base, int32_t offset) {
    xb_vecNx16 r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint8_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LANX8U_IP(r1, align, (const xb_vecNx8U*)ptr8);
    // Pointer is automatically incremented by previous call.
    IVP_LANX8U_IP(r2, align, (const xb_vecNx8U*)ptr8);

    return uint16x64_t(uint16x64_t::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x16_t widening_load<int32x16_t, int16_t>(const void *base, int32_t offset) {
    int32x16_t r1;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16*)ptr8);
    return r1;
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t widening_load<int32x32_t, int16_t>(const void *base, int32_t offset) {
    int32x16_t r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const int16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16S_IP(r1, align, (const xb_vecN_2x16*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16S_IP(r2, align, (const xb_vecN_2x16*)ptr8);

    return int32x32_t(int32x32_t::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t widening_load<int32x32_t, uint16_t>(const void *base, int32_t offset) {
    int32x16_t r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);

    return int32x32_t(int32x32_t::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint32x32_t widening_load<uint32x32_t, uint16_t>(const void *base, int32_t offset) {
    uint32x16_t r1, r2;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);

    return uint32x32_t(uint32x32_t::from_native_vector, r1, r2);
}

template<>
HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x64_t widening_load<int32x64_t, uint16_t>(const void *base, int32_t offset) {
    int32x16_t r1, r2, r3, r4;
    const xb_vec2Nx8* __restrict ptr8 = (const xb_vec2Nx8*)((const uint16_t*)base + offset);
    valign align = IVP_LA_PP(ptr8);
    IVP_LAN_2X16U_IP(r1, align, (const xb_vecN_2x16U*)ptr8);
    // Pointers is automatically incremented by previous call.
    IVP_LAN_2X16U_IP(r2, align, (const xb_vecN_2x16U*)ptr8);
    IVP_LAN_2X16U_IP(r3, align, (const xb_vecN_2x16U*)ptr8);
    IVP_LAN_2X16U_IP(r4, align, (const xb_vecN_2x16U*)ptr8);

    return int32x64_t(int32x64_t::from_native_vector, r1, r2, r3, r4);
}

template <typename VectorType, typename BaseType, int Lanes>
HALIDE_ALWAYS_INLINE void store_narrowing(const VectorType& a, void *base, int32_t offset) = delete;

template<>
HALIDE_ALWAYS_INLINE void store_narrowing<int16x32_t, uint8_t, 32>(const int16x32_t& a, void *base, int32_t offset) {
	valign align;
	xb_vecNx8U* __restrict ptr  = (xb_vecNx8U*)((uint8_t*)base + offset);
	IVP_SANX8U_IP(a, align, ptr);
	IVP_SAPOSNX8U_FP(align, ptr);
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_interleave_i16(const int16x32_t& a, const int16x32_t& b) {
  return int16x64_t(int16x64_t::from_native_vector,
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE uint16x64_t halide_xtensa_interleave_u16(const uint16x32_t& a, const uint16x32_t& b) {
  return uint16x64_t(uint16x64_t::from_native_vector,
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE uint16x128_t halide_xtensa_interleave_u16(const uint16x32_t& a, const uint16x32_t& b, const uint16x32_t& c, const uint16x32_t& d) {
  const uint16x32_t ab0 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_LO);
  const uint16x32_t ab1 = IVP_SELNX16UI(b, a, IVP_SELI_16B_INTERLEAVE_1_HI);
  const uint16x32_t cd0 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_LO);
  const uint16x32_t cd1 = IVP_SELNX16UI(d, c, IVP_SELI_16B_INTERLEAVE_1_HI);


  return uint16x128_t(uint16x128_t::from_native_vector,
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd0, ab0, IVP_SELI_16B_INTERLEAVE_2_HI),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_LO),
                                IVP_SELNX16UI(cd1, ab1, IVP_SELI_16B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE uint8x128_t halide_xtensa_interleave_u8(const uint8x64_t& a, const uint8x64_t& b) {
  return uint8x128_t(uint8x128_t::from_native_vector,
                                IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO),
                                IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE uint8x256_t halide_xtensa_interleave_u8(const uint8x64_t& a, const uint8x64_t& b, const uint8x64_t& c, const uint8x64_t& d) {
  const uint8x64_t ab0 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_LO);
  const uint8x64_t ab1 = IVP_SEL2NX8UI(b, a, IVP_SELI_8B_INTERLEAVE_1_HI);
  const uint8x64_t cd0 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_LO);
  const uint8x64_t cd1 = IVP_SEL2NX8UI(d, c, IVP_SELI_8B_INTERLEAVE_1_HI);


  return uint8x256_t(uint8x256_t::from_native_vector,
                                IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_LO),
                                IVP_SEL2NX8UI(cd0, ab0, IVP_SELI_8B_INTERLEAVE_2_HI),
                                IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_LO),
                                IVP_SEL2NX8UI(cd1, ab1, IVP_SELI_8B_INTERLEAVE_2_HI));
}

HALIDE_ALWAYS_INLINE uint1x256_t halide_xtensa_interleave_u1(const uint1x64_t& a, const uint1x64_t& b, const uint1x64_t& c, const uint1x64_t& d) {
    uint8x64_t a8 = 0, b8 = 0, c8 = 0, d8 = 0;
    IVP_INJBI2NX8(a8, a, 0);
    IVP_INJBI2NX8(b8, b, 0);
    IVP_INJBI2NX8(c8, c, 0);
    IVP_INJBI2NX8(d8, d, 0);

    uint8x256_t interleaved8 = halide_xtensa_interleave_u8(a8, b8, c8, d8);

    uint1x64_t ra = IVP_EXTBI2NX8(interleaved8.native_vector[0], 0);
    uint1x64_t rb = IVP_EXTBI2NX8(interleaved8.native_vector[1], 0);
    uint1x64_t rc = IVP_EXTBI2NX8(interleaved8.native_vector[2], 0);
    uint1x64_t rd = IVP_EXTBI2NX8(interleaved8.native_vector[3], 0);

    return uint1x256_t(uint1x256_t::from_native_vector, ra, rb, rc, rd);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_extract_0_off_3_u8(const uint8x64_t& a0, const uint8x64_t& a1, const uint8x64_t& a2) {
  // TODO(vksnk): there is likely a better way to do it.
  uint8x64_t vR, vG, vB, vRG0, vRG1;
  IVP_DSEL2NX8UI(vB, vRG0, a1, a0, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_0);
  IVP_DSEL2NX8UI_H(vB, vRG1, a2, a1, IVP_DSELI_8B_DEINTERLEAVE_C3_STEP_1);
  IVP_DSEL2NX8UI (vG,vR, vRG1,vRG0, IVP_DSELI_8B_DEINTERLEAVE_1);
  return vR;
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_extract_0_off_3_u8(const uint8x192_t& a) {
  return halide_xtensa_extract_0_off_3_u8(a.native_vector[0], a.native_vector[1], a.native_vector[2]);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_deinterleave_even_i16(const int16x64_t& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_deinterleave_odd_i16(const int16x64_t& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_deinterleave_even_u16(const uint16x64_t& a) {
  return  IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_deinterleave_odd_u16(const uint16x64_t& a) {
  return  IVP_SELNX16UI(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_i16(const int16x64_t& a, int start) {
  return IVP_SELNX16(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + int16x32_t(start));
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_slice_u16(const uint16x64_t& a, int start) {
  return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + int16x32_t(start));
}

/*
HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_deinterleave_even_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_deinterleave_odd_i8(const int8x128_t& a) {
  return  IVP_SEL2NX8I(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}
*/
HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_deinterleave_even_u8(const uint8x128_t& a) {
  return  IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_deinterleave_odd_u8(const uint8x128_t& a) {
  return  IVP_SEL2NX8UI(a.native_vector[1], a.native_vector[0], IVP_SELI_8B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE float32x16_t halide_xtensa_slice_f32(const float32x32_t& a, int start) {
  return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], IVP_ADDN_2X32(IVP_SEQN_2X32(), int32x16_t(start)));
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_dynamic_shuffle(const uint8x64_t& a, const int8x64_t& b) {
  return IVP_SHFL2NX8U(a, b);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_dynamic_shuffle(const uint8x128_t& a, const int8x64_t& b) {
  return IVP_SEL2NX8(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_dynamic_shuffle(const int16x32_t& a, const int16x32_t& b) {
  return IVP_SHFLNX16(a, b);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_dynamic_shuffle(const uint16x32_t& a, const int16x32_t& b) {
  return IVP_SHFLNX16U(a, b);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_dynamic_shuffle(const int16x64_t& a, const int16x32_t& b) {
  return IVP_SELNX16(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_dynamic_shuffle(const uint16x64_t& a, const int16x32_t& b) {
  return IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_dynamic_shuffle(const int16x64_t& a, const int16x64_t& b) {
  return int16x64_t(int16x64_t::from_native_vector,
                    IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                    IVP_SELNX16(a.native_vector[1], a.native_vector[0], b.native_vector[1])
                  );
}

HALIDE_ALWAYS_INLINE uint16x64_t halide_xtensa_dynamic_shuffle(const uint16x64_t& a, const int16x64_t& b) {
  return uint16x64_t(uint16x64_t::from_native_vector,
                    IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[0]),
                    IVP_SELNX16U(a.native_vector[1], a.native_vector[0], b.native_vector[1])
                  );
}

HALIDE_ALWAYS_INLINE float32x16_t halide_xtensa_dynamic_shuffle(const float32x16_t& a, const int32x16_t& b) {
  return IVP_SHFLN_2XF32(a, b);
}

HALIDE_ALWAYS_INLINE float32x16_t halide_xtensa_dynamic_shuffle(const float32x32_t& a, const int32x16_t& b) {
  return IVP_SELN_2XF32(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_sat_add_i32(const int32x16_t& a,
                                                                      const int32x16_t& b) {
  // I am not 100% about it.
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = IVP_MULN_2X32(a, one);
  IVP_MULAN_2X32(l0, b, one);
  return IVP_PACKVRN_2X64W(l0, 0);
}

HALIDE_ALWAYS_INLINE int32x32_t halide_xtensa_sat_add_i32(const int32x32_t& a,
                                                                      const int32x32_t& b) {
  // I am not 100% about it.
  xb_vecN_2x32v zero = 0;
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = a.native_vector[0] * one;
  IVP_MULAN_2X32(l0, b.native_vector[0], one);
  xb_vecN_2x64w l1 = a.native_vector[1] * one;
  IVP_MULAN_2X32(l1, b.native_vector[1], one);
  return int32x32_t(int32x32_t::from_native_vector, IVP_PACKVN_2X64W(l0, zero), IVP_PACKVN_2X64W(l1, zero));
  //return a + b;
  /*
  // determine the lower or upper bound of the result
  //int64_t ret =  (x < 0) ? INT64_MIN : INT64_MAX;
  int32x32_t ret = int32x32_t::select(a < int32x32_t::broadcast(0),
                                      int32x32_t::broadcast(INT32_MIN),
                                      int32x32_t::broadcast(INT32_MAX));
  // this is always well defined:
  // if x < 0 this adds a positive value to INT64_MIN
  // if x > 0 this subtracts a positive value from INT64_MAX
  int32x32_t comp = ret - a;
  // the condition is equivalent to
  // ((x < 0) && (y > comp)) || ((x >=0) && (y <= comp))
  //if ((x < 0) == (y > comp)) ret = x + y;
  ret = int32x32_t::select(IVP_NOTBN(IVP_XORBN(a < int32x32_t::broadcast(0), comp <= b)), a + b, ret);
  return ret;
  */
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_add_i16(const int16x32_t& a, const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c) {
  int16x32_t r = a;
  IVP_ADDNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_sub_i16(const int16x32_t& a, const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c) {
  int16x32_t r = a;
  IVP_SUBNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_max_i16(const int16x32_t& a, const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c) {
  int16x32_t r = a;
  IVP_MAXNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_min_i16(const int16x32_t& a, const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c) {
  int16x32_t r = a;
  IVP_MINNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_sat_add_i16(const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c, const int16x32_t& a) {
  int16x32_t r = a;
  IVP_ADDSNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_pred_sat_sub_i16(const int16x32_t& a, const uint1x32_t& p, const int16x32_t& b, const int16x32_t& c) {
  int16x32_t r = a;
  IVP_SUBSNX16T(r, b, c, p);
  return r;
}

HALIDE_ALWAYS_INLINE int64x16_t halide_xtensa_widen_mul_i64(const int32x16_t& a, const int32x16_t& b) {
  return IVP_MULN_2X32(a, b);
}

HALIDE_ALWAYS_INLINE int64x16_t halide_xtensa_widen_mul_add_i64(const int32x16_t& a, const int32x16_t& b, const int32x16_t& c) {
  xb_vecN_2x64w r = IVP_MULN_2X32(c, int32x16_t(1));
  IVP_MULAN_2X32(r, a, b);
  return r;
}


HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_mul_add_i48(const int48x32_t& a, const int16x32_t& b, const int16x32_t& c) {
  int48x32_t r = a;
  IVP_MULANX16(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_mul_add_u24(const int24x64_t& a, const uint8x64_t& b, const uint8x64_t& c) {
  int24x64_t r = a;
  IVP_MULUUA2NX8(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_mul_add_i24(const int24x64_t& a, const int8x64_t& b, const int8x64_t& c) {
  int24x64_t r = a;
  IVP_MULA2NX8(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_i24(
                                            const int24x64_t& acc,
                                            const int8x64_t& a0,
                                            const int8_t& s0,
                                            const int8x64_t& a1,
                                            const int8_t& s1,
                                            const int8x64_t& a2,
                                            const int8_t& s2,
                                            const int8x64_t& a3,
                                            const int8_t& s3
                                            ) {
  int24x64_t r = acc;
  const int8_t scalar_coef[] = {s3, s2, s1, s0};
  const xb_int32pr * __restrict coef = (const xb_int32pr*)scalar_coef;
  IVP_MULQA2N8XR8(r, a0, a1, a2, a3, coef[0]);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_i24(
                                            const int24x64_t& acc,
                                            const int8x64_t& a0,
                                            const int8x64_t& a1,
                                            const int8x64_t& a2,
                                            const int8x64_t& a3,
                                            const int8x4_t& s
                                            ) {
  int24x64_t r = acc;
  IVP_MULQA2N8XR8(r, a3, a2, a1, a0, s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_i24(
                                            const int24x64_t& acc,
                                            const int8x256_t& a,
                                            const int8x4_t& s
                                            ) {
  int24x64_t r = acc;
  IVP_MULQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_u24(
                                            const int24x64_t& acc,
                                            const uint8x64_t& a0,
                                            const uint8x64_t& a1,
                                            const uint8x64_t& a2,
                                            const uint8x64_t& a3,
                                            const uint8x4_t& s
                                            ) {
  int24x64_t r = acc;
  IVP_MULUUQA2N8XR8(r, a3, a2, a1, a0, s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_u24(
                                            const int24x64_t& acc,
                                            const uint8x256_t& a,
                                            const uint8x4_t& s
                                            ) {
  int24x64_t r = acc;
  IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_quad_mul_add_by_scalar_u24(
                                            const int24x64_t& acc,
                                            const uint8x256_t& a,
                                            const uint8_t& s
                                            ) {
  const xb_int32pr coef = s | (s << 8) | (s << 16) | (s << 24);

  int24x64_t r = acc;
  IVP_MULUUQA2N8XR8(r, a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], coef);
  return r;
}

HALIDE_ALWAYS_INLINE int24x128_t halide_xtensa_dual_widen_quad_mul_add_i24(
                                            const int24x128_t& acc,
                                            const int8x256_t& a,
                                            const int8x8_t& s) {
  int24x128_t r(acc);
  IVP_DMULQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x128_t halide_xtensa_dual_widen_quad_mul_add_u24(
                                            const int24x128_t& acc,
                                            const uint8x256_t& a,
                                            const uint8x8_t& s) {
  int24x128_t r(acc);
  IVP_DMULUUQA2N8XR8(r.native_vector[1], r.native_vector[0], a.native_vector[3], a.native_vector[2], a.native_vector[1], a.native_vector[0], s);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_pair_mul_i24(const int8x64_t& a, const int8x64_t& b,
                                                                  const int8x64_t& c, const int8x64_t& d) {
  return IVP_MULP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_pair_mul_add_i24(const int24x64_t& a, const int8x64_t& b,
                                                                  const int8x64_t& c, const int8x64_t& d, const int8x64_t& e) {
  int24x64_t r = a;
  IVP_MULPA2NX8(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_pair_mul_add_u24(const int24x64_t& a, const uint8x64_t& b,
                                                                  const uint8x64_t& c, const uint8x64_t& d, const uint8x64_t& e) {
  int24x64_t r = a;
  IVP_MULUUPA2NX8(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_pair_mul_u24(const uint8x64_t& a, const uint8x64_t& b,
                                                                  const uint8x64_t& c, const uint8x64_t& d) {
  return IVP_MULUUP2NX8(a, b, c, d);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_mul_i48(const int16x32_t& a, const int16x32_t& b,
                                                                  const int16x32_t& c, const int16x32_t& d) {
  return IVP_MULPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_mul_add_i48(const int48x32_t& a, const int16x32_t& b,
                                                                  const int16x32_t& c, const int16x32_t& d, const int16x32_t& e) {
  int48x32_t r = a;
  IVP_MULPANX16(r, b, c, d, e);
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_mul_u48(const uint16x32_t& a, const uint16x32_t& b,
                                                                  const uint16x32_t& c, const uint16x32_t& d) {
  return IVP_MULUUPNX16(a, b, c, d);
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_mul_add_by_diff_u24(const int24x64_t& a, const uint8x64_t& d1,
                                                                  const uint8x64_t& d2, const uint8x64_t& c) {
  int24x64_t r = a;
  IVP_MULUUPDA2NX8(r, d1, c, d2, c);
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_add_i48(const int16x32_t& a, const int16x32_t& b) {
  return IVP_ADDWNX16(a, b);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_add_i48(const int48x32_t& a, const int16x32_t& b) {
  int48x32_t r = a;
  IVP_ADDWANX16(r, b, int16x32_t(0));
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_add_i48(const int48x32_t& a, const int16x32_t& b, const int16x32_t& c) {
  int48x32_t r = a;
  IVP_ADDWANX16(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_add_u48(const uint16x32_t& a, const uint16x32_t& b) {
  return IVP_ADDWUNX16U(a, b);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_add_u48(const int48x32_t& a, const uint16x32_t& b) {
  int48x32_t r = a;
  IVP_ADDWUANX16U(r, b, uint16x32_t(0));
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_add_u48(const int48x32_t& a, const uint16x32_t& b, const uint16x32_t& c) {
  int48x32_t r = a;
  IVP_ADDWUANX16U(r, b, c);
  return r;
}
/*
Disabled for now.
HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_mul_vu8_si16_i24(const uint8x64_t& a, const int16_t& b) {
  return IVP_MULUS2N8XR16(a, b);
}

// TODO(vksnk):The one below is incorrect:

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_pair_mul_vu8_si16_i24(
                                                                  const uint8x64_t& a, const int16_t& b,
                                                                  const uint8x64_t& c, const int16_t& d) {
  return IVP_MULUSP2N8XR16(a, c, (b << 16) | d);
}

HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_mul_add_vu8_si16_i24(const int24x64_t& a, const uint8x64_t& b, const int16_t& c) {
  int24x64_t r = a;
  IVP_MULUSA2N8XR16(r, b, c);
  return r;
}
*/
HALIDE_ALWAYS_INLINE int24x64_t halide_xtensa_widen_add_i24(const int24x64_t& a, const int8x64_t& b) {
  int24x64_t r = a;
  IVP_ADDWA2NX8(r, b, int8x64_t(0));
  return r;
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_sat_narrow_i24x_with_shift_i8(const int24x64_t& a, int shift) {
  return IVP_PACKVRNR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_sat_narrow_i24x_with_shift_u8(const int24x64_t& a, int shift) {
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKVRNR2NX24(a, shift));
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_narrow_i24_with_shift_i16(const int24x64_t& a, int shift) {
    int16x32_t even = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_0(a, shift));
    int16x32_t odd = xb_vecNx16U_rtor_xb_vecNx16(IVP_PACKVRNR2NX24_1(a, shift));
    int16x64_t r;
    IVP_DSELNX16I(r.native_vector[1], r.native_vector[0], odd, even, IVP_DSELI_INTERLEAVE_1);
    return r;
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_narrow_i24_with_shift_i8(const int24x64_t& a, int shift) {
  return IVP_PACKVR2NX24(a, shift);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_narrow_i48_with_shift_u16(const int48x32_t& a, int shift) {
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(a, shift));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_narrow_with_shift_i16(const int32x32_t& a, int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNRNX48(wide, shift);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_narrow_with_shift_u16(const int32x32_t& a, int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_narrow_high_i32(const int64x16_t& a) {
  return IVP_PACKHN_2X64W(a);
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_sat_narrow_shift_i32(const int64x16_t& a, int shift) {
  return IVP_PACKVN_2X64W(a, shift);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_narrow_clz_i16(const int32x32_t& a) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(IVP_NSAUN_2X32(a.native_vector[1]), IVP_NSAUN_2X32(a.native_vector[0]));
  return IVP_CVT16U2NX24L(wide);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_narrow_clz_i16(const uint32x32_t& a) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(IVP_NSAUN_2X32(a.native_vector[1]), IVP_NSAUN_2X32(a.native_vector[0]));
  return IVP_CVT16U2NX24L(wide);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_i48x_clz_i16(const int48x32_t& a) {
  xb_vecNx16 clz_lo = IVP_NSAUNX16(IVP_PACKLNX48(a));
  xb_vecNx16 clz_hi = IVP_NSAUNX16(IVP_PACKVRNRNX48(a, 16));
  IVP_ADDNX16T(clz_hi, clz_hi, clz_lo, clz_hi == xb_vecNx16(16));
  return clz_hi;
}

HALIDE_ALWAYS_INLINE uint1x32_t halide_xtensa_i48x_gt_zero(const int48x32_t& b) {
  return int16x32_t(0) < IVP_PACKVRNX48(b, 0);
}

HALIDE_ALWAYS_INLINE uint1x32_t halide_xtensa_i16_neq_zero(const int16x32_t& a) {
  return IVP_NEQNX16(a, int16x32_t(0));
}

HALIDE_ALWAYS_INLINE int32_t halide_xtensa_full_reduce_add_u8_to_i32(const uint8x64_t& a) {
    return xb_int16U_rtor_uint16(IVP_RADDU2NX8(a));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_lerp_i16(const int16x32_t& a, const int16x32_t& b, uint16_t w) {
  // TODO(vksnk): Halide lerp actually uses full range, but it's not clear from the documentation
  // if we can pass unsigned type to IVP_MULPN16XR16, so just to be extra careful reduce it to 14-bit
  // for now.
  uint32_t w32 = ((uint32_t(w)) >> 0);
  uint32_t alphaMalpha = ((65536 - w32) << 16) | w32;
  xb_vecNx48 output = IVP_MULSUPN16XR16(a, b, alphaMalpha);
  IVP_DECNEGWNX48(output);
  return IVP_PACKVRNX48(output, 16);
}

/*
HALIDE_ALWAYS_INLINE uint16x64_t convert_to_uint16x64_t_from_uint8x64_t(const uint8x64_t& src) {
  xb_vec2Nx24 wide = src * uint8x64_t(1);
  return uint16x64_t(uint16x64_t::from_native_vector,
                        IVP_CVT16U2NX24L(wide), IVP_CVT16U2NX24H(wide));
}

HALIDE_ALWAYS_INLINE int16x64_t convert_to_int16x64_t_from_uint8x64_t(const uint8x64_t& src) {
  xb_vec2Nx24 wide = src * uint8x64_t(1);
  return int16x64_t(int16x64_t::from_native_vector,
                        IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}
*/
HALIDE_ALWAYS_INLINE int16x64_t convert_to_int16x64_t_from_int24x64_t(const int24x64_t& wide) {
  return int16x64_t(int16x64_t::from_native_vector,
                        IVP_CVT16S2NX24L(wide), IVP_CVT16S2NX24H(wide));
}

HALIDE_ALWAYS_INLINE int8x64_t convert_to_int8x64_t_from_int16x64_t(const int16x64_t& src) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE uint8x64_t convert_to_uint8x64_t_from_int16x64_t(const int16x64_t& src) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(src.native_vector[1], src.native_vector[0]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

HALIDE_ALWAYS_INLINE int8x64_t convert_to_int8x64_t_from_int32x64_t(const int32x64_t& src) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
  IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE uint8x64_t convert_to_uint8x64_t_from_int32x64_t(const int32x64_t& src) {
  xb_vec2Nx24 wide = IVP_CVT24UNX32L(src.native_vector[1], src.native_vector[0]);
  IVP_CVT24UNX32H(wide, src.native_vector[3], src.native_vector[2]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

HALIDE_ALWAYS_INLINE uint8x64_t convert_to_uint8x64_t_from_uint16x64_t(const uint16x64_t& src) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(src.native_vector[1], src.native_vector[0]);
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

HALIDE_ALWAYS_INLINE int16x32_t convert_to_int16x32_t_from_int32x32_t(const int32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

HALIDE_ALWAYS_INLINE int48x32_t convert_to_int48x32_t_from_int32x32_t(const int32x32_t& src) {
  return IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
}

HALIDE_ALWAYS_INLINE int16x32_t convert_to_int16x32_t_from_uint32x32_t(const uint32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

HALIDE_ALWAYS_INLINE int16x64_t convert_to_int16x64_t_from_int32x64_t(const int32x64_t& src) {
  xb_vecNx48 wide0 = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  xb_vecNx48 wide1 = IVP_CVT48SNX32(src.native_vector[3], src.native_vector[2]);

  return int16x64_t(int16x64_t::from_native_vector, IVP_PACKLNX48(wide0), IVP_PACKLNX48(wide1));
}

HALIDE_ALWAYS_INLINE uint16x32_t convert_to_uint16x32_t_from_int32x32_t(const int32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE uint16x32_t convert_to_uint16x32_t_from_uint32x32_t(const uint32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE int32x16_t convert_to_int32x16_t_from_uint1x16_t(const uint1x16_t& src) {
  xb_vecN_2x32v r = 0;
  IVP_INJBIN_2X32(r, src, 0);
  return r;
}

HALIDE_ALWAYS_INLINE int32x64_t convert_to_int32x64_t_from_uint8x64_t(const uint8x64_t& src) {
    xb_vec2Nx24 wide = src * uint8x64_t(1);
    return int32x64_t(int32x64_t::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                                      IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

HALIDE_ALWAYS_INLINE uint32x64_t convert_to_uint32x64_t_from_uint8x64_t(const uint8x64_t& src) {
    xb_vec2Nx24 wide = src * uint8x64_t(1);
    return uint32x64_t(uint32x64_t::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide),
                                                      IVP_CVT32S2NX24HL(wide), IVP_CVT32S2NX24HH(wide));
}

HALIDE_ALWAYS_INLINE int32x64_t convert_to_int32x64_t_from_int24x64_t(const int24x64_t& src) {
    return int32x64_t(int32x64_t::from_native_vector, IVP_CVT32S2NX24LL(src), IVP_CVT32S2NX24LH(src),
                                                      IVP_CVT32S2NX24HL(src), IVP_CVT32S2NX24HH(src));
}

HALIDE_ALWAYS_INLINE int32x32_t convert_to_int32x32_t_from_int16x32_t(const int16x32_t& src) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src);
    return int32x32_t(int32x32_t::from_native_vector,
                      IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
}

HALIDE_ALWAYS_INLINE int32x64_t convert_to_int32x64_t_from_int16x64_t(const int16x64_t& src) {
    auto r0 = convert_to_int32x32_t_from_int16x32_t(src.native_vector[0]);
    auto r1 = convert_to_int32x32_t_from_int16x32_t(src.native_vector[1]);

    return int32x64_t(int32x64_t::from_native_vector, r0.native_vector[0], r0.native_vector[1],
                                                      r1.native_vector[0], r1.native_vector[1]);
}

HALIDE_ALWAYS_INLINE int32x32_t convert_to_int32x32_t_from_uint16x32_t(const uint16x32_t& src) {
  return int32x32_t(int32x32_t::from_native_vector,
                    IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_LO)),
                    IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_HI)));
}

HALIDE_ALWAYS_INLINE int32x32_t convert_to_int32x32_t_from_uint32x32_t(const uint32x32_t& src) {
    return int32x32_t(int32x32_t::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

HALIDE_ALWAYS_INLINE uint32x32_t convert_to_uint32x32_t_from_int32x32_t(const int32x32_t& src) {
    return uint32x32_t(uint32x32_t::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

HALIDE_ALWAYS_INLINE uint16x64_t convert_to_uint16x64_t_from_int16x64_t(const int16x64_t& src) {
    return uint16x64_t(uint16x64_t::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

HALIDE_ALWAYS_INLINE int32x32_t convert_to_int32x32_t_from_int48x32_t(const int48x32_t& src) {
    return int32x32_t(int32x32_t::from_native_vector,
                                IVP_CVT32SNX48L(src),
                                IVP_CVT32SNX48H(src));
}

HALIDE_ALWAYS_INLINE uint32x32_t convert_to_uint32x32_t_from_uint16x32_t(const uint16x32_t& src) {
    xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, xb_vecNx16U_rtor_xb_vecNx16(src));
    return uint32x32_t(uint32x32_t::from_native_vector,
                        xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LL(wide)),
                        xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32S2NX24LH(wide)));
}

HALIDE_ALWAYS_INLINE uint32x32_t convert_to_uint32x32_t_from_int48x32_t(const int48x32_t& src) {
    return uint32x32_t(uint32x32_t::from_native_vector,
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src)),
                                xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src)));
}

HALIDE_ALWAYS_INLINE int16x64_t convert_to_int16x64_t_from_uint16x64_t(const uint16x64_t& src) {
    return int16x64_t(int16x64_t::from_native_vector, src.native_vector[0], src.native_vector[1]);
}


HALIDE_ALWAYS_INLINE float32x16_t convert_to_float32x16_t_from_int32x16_t(const int32x16_t& src) {
  return IVP_FLOATN_2X32(src, 0);
}

HALIDE_ALWAYS_INLINE float32x32_t convert_to_float32x32_t_from_int32x32_t(const int32x32_t& src) {
  return float32x32_t(float32x32_t::from_native_vector,
                  convert_to_float32x16_t_from_int32x16_t(src.native_vector[0]),
                  convert_to_float32x16_t_from_int32x16_t(src.native_vector[1]));
}

HALIDE_ALWAYS_INLINE float32x32_t convert_to_float32x32_t_from_int16x32_t(const int16x32_t& src) {
    int32x32_t tmp = convert_to_int32x32_t_from_int16x32_t(src);
    return convert_to_float32x32_t_from_int32x32_t(tmp);
}

HALIDE_ALWAYS_INLINE int32x16_t convert_to_int32x16_t_from_float32x16_t(const float32x16_t& src) {
  return IVP_TRUNCN_2XF32(src, 0);
}

HALIDE_ALWAYS_INLINE int32x32_t convert_to_int32x32_t_from_float32x32_t(const float32x32_t& src) {
  return int32x32_t(int32x32_t::from_native_vector,
                  convert_to_int32x16_t_from_float32x16_t(src.native_vector[0]),
                  convert_to_int32x16_t_from_float32x16_t(src.native_vector[1]));
}

HALIDE_ALWAYS_INLINE int16x32_t convert_to_int16x32_t_from_float32x32_t(const float32x32_t& src) {
    int32x32_t tmp = convert_to_int32x32_t_from_float32x32_t(src);
    return convert_to_int16x32_t_from_int32x32_t(tmp);
}


HALIDE_ALWAYS_INLINE uint1x16_t halide_xtensa_slice_to_native(const uint1x32_t& src, int index, int native_lanes, int total_lanes) {
  return (index == 0)?IVP_EXTRACTBLN(src):IVP_EXTRACTBHN(src);
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_convert_i16_low_i32(const int16x32_t& src) {
    const int32x16_t m = int32x16_t(1U << (16 - 1));
    int32x16_t x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(int16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
    int32x16_t r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_convert_i16_high_i32(const int16x32_t& src) {
    const int32x16_t m = int32x16_t(1U << (16 - 1));
    int32x16_t x = IVP_MOVN_2X32_FROMNX16(IVP_SELNX16I(int16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
    int32x16_t r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_convert_u16_low_i32(const uint16x32_t& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_convert_u16_high_i32(const uint16x32_t& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_convert_u16_low_u32(const uint16x32_t& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_convert_u16_high_u32(const uint16x32_t& src) {
    return IVP_MOVN_2X32_FROMNX16(IVP_SELNX16UI(uint16x32_t(0), src, IVP_SELI_16B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_i32_u16(const int32x16_t& src0, const int32x16_t& src1) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src1, src0);
  return xb_vecNx16_rtor_xb_vecNx16U(IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_convert_concat_i16_to_i8(const int16x32_t& a, const int16x32_t& b) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(b, a);
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_sat_narrow_u8(const int16x64_t& a) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRU2NX24(wide, 0);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_sat_narrow_i16(const int32x32_t& a) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNX48(wide, 0);
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_sat_narrow_with_shift_i8(const int16x64_t& a, uint32_t shift) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVR2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_sat_narrow_with_shift_u8(const int16x64_t& a, uint32_t shift) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRU2NX24(wide, shift);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_sat_narrow_with_shift_i16(const int32x32_t& a, uint32_t shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNX48(wide, shift);
}

// TODO(vksnk): this is pretty inefficient.
HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_sat_narrow_with_signed_shift_i16(const int32x32_t& a, int32_t shift) {
  if (shift >= 0) {
    return halide_xtensa_sat_narrow_with_shift_i16(a, (uint32_t)shift);
  }

  return halide_xtensa_sat_narrow_i16(
            int32x32_t(int32x32_t::from_native_vector,
                        IVP_SLAN_2X32(a.native_vector[0], -shift),
                        IVP_SLAN_2X32(a.native_vector[1], -shift)));
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_sat_narrow_with_shift_i32(const int64x16_t& a, uint32_t shift) {
  return IVP_PACKVRN_2X64W(a, shift);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_convert_concat_i16_to_u8(const int16x32_t& a, const int16x32_t& b) {
  return IVP_SEL2NX8UI(IVP_MOV2NX8_FROMNX16(b), IVP_MOV2NX8_FROMNX16(a), IVP_SELI_8B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE int8x64_t halide_xtensa_convert_concat_u16_to_i8(const uint16x32_t& a, const uint16x32_t& b) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(xb_vecNx16U_rtor_xb_vecNx16(b), xb_vecNx16U_rtor_xb_vecNx16(a));
  return IVP_PACKL2NX24(wide);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_convert_concat_u16_to_u8(const uint16x32_t& a, const uint16x32_t& b) {
  xb_vec2Nx24 wide = IVP_CVT24U2NX16(xb_vecNx16U_rtor_xb_vecNx16(b), xb_vecNx16U_rtor_xb_vecNx16(a));
  return xb_vec2Nx8_rtor_xb_vec2Nx8U(IVP_PACKL2NX24(wide));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_i8_low_i16(const int8x64_t& src, int native_lanes, int total_lines) {
    const int16x32_t m = int16x32_t(1U << (8 - 1));
    int16x32_t x =  IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(int8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
    int16x32_t r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_i8_high_i16(const int8x64_t& src, int native_lanes, int total_lines) {
    const int16x32_t m = int16x32_t(1U << (8 - 1));
    int16x32_t x =  IVP_MOVNX16_FROM2NX8(IVP_SEL2NX8I(int8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
    int16x32_t r = (x ^ m) - m;
    return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_u8_low_i16(const uint8x64_t& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(uint8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_u8_high_i16(const uint8x64_t& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(uint8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_u8_low_u16(const uint8x64_t& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(uint8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_LO));
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_u8_high_u16(const uint8x64_t& src, int native_lanes, int total_lines) {
    return IVP_MOVNX16_FROM2NX8U(IVP_SEL2NX8UI(uint8x64_t(0), src, IVP_SELI_8B_INTERLEAVE_1_HI));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_concat_i32_to_i16(const int32x16_t& a, const int32x16_t& b) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_concat_i32_to_u16(const int32x16_t& a, const int32x16_t& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32(b), IVP_MOVNX16_FROMN_2X32(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_concat_u32_to_i16(const uint32x16_t& a, const uint32x16_t& b) {
  return IVP_SELNX16I(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_concat_u32_to_u16(const uint32x16_t& a, const uint32x16_t& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_concat_u32_to_u16_zzz(const uint32x16_t& a, const uint32x16_t& b) {
  return IVP_SELNX16UI(IVP_MOVNX16_FROMN_2X32U(b), IVP_MOVNX16_FROMN_2X32U(a), IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_convert_i48_low_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48L(src));
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_convert_i48_high_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_CVT32UNX48H(src));
}

HALIDE_ALWAYS_INLINE uint1x32_t halide_xtensa_concat_from_native(const uint1x16_t& a, const uint1x16_t& b) {
        return IVP_JOINBN_2(b, a);
}

HALIDE_ALWAYS_INLINE uint1x64_t halide_xtensa_concat_from_native(const uint1x32_t& a, const uint1x32_t& b) {
        return IVP_JOINBN(b, a);
}

HALIDE_ALWAYS_INLINE uint1x64_t halide_xtensa_concat_from_native(const uint1x16_t& a, const uint1x16_t& b, const uint1x16_t& c, const uint1x16_t& d) {
    return halide_xtensa_concat_from_native(halide_xtensa_concat_from_native(a, b), halide_xtensa_concat_from_native(c, d));
}

HALIDE_ALWAYS_INLINE float32x32_t halide_xtensa_concat_from_native(const float32x16_t& a, const float32x16_t& b) {
    return float32x32_t(float32x32_t::from_native_vector, a, b);
}

// TODO(vksnk): this is disabled by default, because iDMA is not part of cstub
// so we need to get git repo compiling with xt-tools first (b/173159625)

#ifdef __cplusplus
extern "C" {
#endif

extern void *halide_tcm_malloc(void *user_context, size_t x);
extern void halide_tcm_free(void *user_context, void *ptr);
extern int halide_init_dma();
extern int32_t halide_xtensa_copy_1d(void* dst, int32_t dst_base, void* src, int32_t src_base, int extent, int item_size);
extern int32_t halide_xtensa_wait_for_copy(int32_t id);
extern int halide_release_dma();

#ifdef __cplusplus
}  // extern "C"
#endif

class ScopedDmaInitializer {
  public:
  ScopedDmaInitializer() {
    int status = halide_init_dma();
    printf("FROM DEVICE: IDMA Init with status %d\n", status);
  }

  ~ScopedDmaInitializer() {
    halide_release_dma();
    printf("FROM DEVICE: IDMA release \n");
  }
};

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
            Type(Type::Int, 8, 64),
            Type(Type::UInt, 8, 64),
            Type(Type::Int, 16, 32),
            Type(Type::UInt, 16, 32),
            Type(Type::Int, 32, 16),
            Type(Type::UInt, 32, 16),
            Type(Type::Int, 24, 64),
            Type(Type::UInt, 24, 64),
            Type(Type::Int, 48, 32),
            Type(Type::UInt, 48, 32),
            Type(Type::Int, 64, 16),
            Type(Type::Float, 16, 32),
            Type(Type::Float, 32, 16),
        };

        std::set<Type> predefined_vectors = {
            Int(8, 4),
            Int(8, 128),
            UInt(8, 4),
            UInt(8, 8),
            UInt(8, 128),
            UInt(8, 192),
            Int(8, 256),
            UInt(8, 256),
            Int(16, 64),
            UInt(16, 64),
            Int(16, 128),
            UInt(16, 128),
            Int(24, 128),
            UInt(24, 128),
            Int(32, 32),
            UInt(32, 32),
            Int(32, 64),
            UInt(32, 64),
            Float(32, 32),
            Int(48, 32),
            UInt(48, 32),
            Int(48, 64),
            UInt(48, 64),
        };

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

// TODO(vksnk): condense this code.
bool CodeGen_Xtensa::is_native_vector_type(Type t) {
    if (t.is_int_or_uint() && (t.lanes() == 64) && (t.bits() == 8)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 64) && (t.bits() == 24)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 32) && (t.bits() == 16)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 32) && (t.bits() == 48)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 16) && (t.bits() == 32)) {
        return true;
    }

    if (t.is_float() && (t.lanes() == 16) && (t.bits() == 32)) {
        return true;
    }

    return false;
}

string CodeGen_Xtensa::print_assignment(Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        stream << get_indent() << print_type(t, AppendSpace) << (t.is_handle() ? " __restrict " : "") << (output_kind == CPlusPlusImplementation ? "const " : "") << id << " = " << rhs << ";\n";
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
        if (is_native_xtensa_vector<uint8_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLLI2NX8U(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<int8_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLLI2NX8(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLLNX16U(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLANX16(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLLN_2X32U(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SLAN_2X32(" + sa + ", " + std::to_string(bits) + ")");
        } else {
            visit_binop(op->type, op->a, make_const(op->a.type(), bits), "<<");
        }
    } else {
        if (is_native_xtensa_vector<int16_t>(op->type)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_MULNX16PACKL(" + sa + ", " + sb + ")");
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
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

    if (op->name == "halide_xtensa_copy_1d") {
        internal_assert(op->args.size() >= 3);

        const Variable *dest = op->args[0].as<Variable>();
        internal_assert(dest != nullptr);
        args[0] = print_name(dest->name);
        args[1] = print_expr(op->args[1]);
        const Variable *src = op->args[2].as<Variable>();
        internal_assert(src != nullptr);
        args[2] = print_name(src->name);

        for (size_t i = 3; i < op->args.size(); i++) {
            args[i] = print_expr(op->args[i]);
        }
        rhs << op->name << "(" << with_commas(args) << ")";
        return rhs.str();
    }

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
        if (op->type.is_bool()) {
            internal_assert((op->type.lanes() == 64 && op->args[0].type().lanes() == 32) || (op->type.lanes() == 32 && op->args[0].type().lanes() == 16) || (op->type.lanes() == 64 && op->args[0].type().lanes() == 16)) << Expr(op);
        }
        rhs << op->name << "<" << print_type(op->args[0].type()) << ", "
            << print_type(op->type) << ", " << print_type(op->type.element_of())
            << ", " << op->args[0].type().lanes() << ", " << op->type.lanes()
            << ">(" << args[0] << ", " << args[1] << ")";
        return rhs.str();
    }

    if (op->name == "halide_xtensa_slice_to_native" && !op->type.is_bool()) {
        Type native_vector_type = get_native_xtensa_vector(op->type);
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
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            intrinsic_name = "IVP_SEL2NX8I";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            intrinsic_name = "IVP_SEL2NX8UI";
            shift_define = "IVP_SELI_8B_ROTATE_";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            intrinsic_name = "IVP_SELNX16I";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            intrinsic_name = "IVP_SELNX16UI";
            shift_define = "IVP_SELI_16B_ROTATE_";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            intrinsic_name = "IVP_SELN_2X32I";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            intrinsic_name = "IVP_SELN_2X32UI";
            shift_define = "IVP_SELI_32B_ROTATE_";
        } else if (is_native_xtensa_vector<float>(op->type)) {
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

    string op_name = op->name;
    std::map<string, string> op_name_to_intrinsic = {
        {"halide_xtensa_sat_add_i16", "IVP_ADDSNX16"},
        {"halide_xtensa_sat_sub_i16", "IVP_SUBSNX16"},
        {"halide_xtensa_avg_i16", "IVP_AVGNX16"},
        {"halide_xtensa_avg_u16", "IVP_AVGUNX16"},
        {"halide_xtensa_avg_round_i16", "IVP_AVGRNX16"},
        {"halide_xtensa_avg_round_u16", "IVP_AVGRUNX16U"},
        {"halide_xtensa_widen_mul_i48", "IVP_MULNX16"},
        {"halide_xtensa_widen_mul_u48", "IVP_MULUUNX16"},
        {"halide_xtensa_widen_mul_ui48", "IVP_MULUSNX16"},
        {"halide_xtensa_widen_pair_mul_u48", "IVP_MULUUPNX16"},
        {"halide_xtensa_convert_i48_low_i32", "IVP_CVT32SNX48L"},
        {"halide_xtensa_convert_i48_high_i32", "IVP_CVT32SNX48H"},
        {"halide_xtensa_convert_i48_low_u32", "IVP_CVT32UNX48L"},
        {"halide_xtensa_convert_i48_high_u32", "IVP_CVT32UNX48H"},
        {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", "convert_to_int32x16_t_from_uint1x16_t"},
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
        if (is_native_xtensa_vector<uint16_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRLNX16U(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRANX16(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRLN_2X32U(" + sa + ", " + std::to_string(bits) + ")");
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRAN_2X32(" + sa + ", (int32x16_t)" + std::to_string(bits) + ")");
        } else {
            visit_binop(op->type, op->a, make_const(op->a.type(), bits), ">>");
        }
        // } else if (op->type.is_int()) {
        //     print_expr(lower_euclidean_div(op->a, op->b));
    } else if (is_native_xtensa_vector<float>(op->type)) {
        ostringstream rhs;
        rhs << "IVP_DIVN_2XF32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    } else {
        string sa = print_expr(op->a);
        string sb = print_expr(op->b);
        if (is_native_xtensa_vector<int32_t>(op->type)) {
            print_assignment(op->type, "(common_int32x16_t)" + sa + " / (common_int32x16_t)" + sb);
        } else {
            print_assignment(op->type, sa + " / " + sb);
        }
    }
}

void CodeGen_Xtensa::visit(const Mod *op) {
    if (is_native_xtensa_vector<int32_t>(op->type)) {
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
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MAX2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MAXU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MAXNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MAXUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MAXN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MAXUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
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
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MIN2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MINU2NX8(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MINNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MINUNX16U(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MINN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MINUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
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
        if (is_native_xtensa_vector<int8_t>(op->type)) {
            rhs << "IVP_MOV2NX8T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint8_t>(op->type)) {
            rhs << "IVP_MOV2NX8UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int16_t>(op->type)) {
            rhs << "IVP_MOVNX16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
            rhs << "IVP_MOVNX16UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<int32_t>(op->type)) {
            rhs << "IVP_MOVN_2X32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
            rhs << "IVP_MOVN_2X32UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (is_native_xtensa_vector<float>(op->type)) {
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
    if (is_const_one(op->stride)) {
        if (is_native_xtensa_vector<int32_t>(op->type)) {
            print_assignment(vector_type, "/* ramp */ int32x16_t(" + id_base + ") + IVP_SEQN_2X32()");
        } else {
            // If it's wide enough split it here into concat of smaller ramps.
            if (op->type.is_int() && (op->type.bits() == 32) && (op->type.lanes() % 16 == 0) && (op->type.lanes() / 16 > 4)) {
                int split_to = op->type.lanes() / 16;

                std::vector<Expr> concat_args;
                for (int ix = 0; ix < split_to; ix++) {
                    Expr r = Ramp::make(op->base + op->stride * (16 * ix), op->stride, 16);
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
        if (is_native_xtensa_vector<int32_t>(op->type)) {
            print_assignment(vector_type, "/* ramp */ int32x16_t(" + id_base + ") + IVP_PACKLN_2X64W(IVP_SEQN_2X32() * int32x16_t(" + id_stride + "))");
        } else if ((op->type.lanes() == 32 || op->type.lanes() == 64) && op->type.is_int_or_uint() && op->type.bits() == 32) {
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

        if (is_native_vector_type(op->type)) {
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

    if (is_native_xtensa_vector<int8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LE2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LEU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LENX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LEUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LEN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LEUN_2X32U(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const LT *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LT2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LTU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LTNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LTUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LTN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_LTUN_2X32U(" + sa + ", " + sb + ")");
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_Xtensa::visit(const GT *op) {
    string sa = print_expr(op->a);
    string sb = print_expr(op->b);

    if (is_native_xtensa_vector<int8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GT2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GTU2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GTNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GTUNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GTN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_GTUN_2X32U(" + sa + ", " + sb + ")");
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

    if (is_native_xtensa_vector<int8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQ2NX8(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint8_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQ2NX8U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQNX16(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint16_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQNX16U(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<int32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQN_2X32(" + sa + ", " + sb + ")");
    } else if (is_native_xtensa_vector<uint32_t>(op->a.type())) {
        print_assignment(op->type, "IVP_EQN_2X32U(" + sa + ", " + sb + ")");
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
        // TODO(vksnk): generalize this!
        int native_lanes = (64 / op->type.element_of().bytes());
        if (op->type.element_of().bytes() == 3) {
            native_lanes = 64;
        }
        if (op->type.element_of().bytes() == 6) {
            native_lanes = 32;
        }
        bool is_aligned_load = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_load = is_aligned_load && (op->param.host_alignment() % 64 == 0);
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
        rhs << "gather_load<" << print_type(t) << ", "
            << print_type(Int(32, t.lanes())) << ", "
            << print_type(t.element_of()) << ", " << t.lanes()
            << ">(" << name << ", " << id_index << ")";
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
        if (cast->value.type().is_vector() && (cast->value.type().bits() == value.type().bits() * 2)) {
            is_narrowing = true;
            value = cast->value;
        }
    }
    if (const Call *call = value.as<Call>()) {
        // TODO: more checks for this one are needed.
        if (call->name == "halide_xtensa_slice_from_padded") {
            if (const Cast *cast = call->args[0].as<Cast>()) {
                if (cast->value.type().is_vector() && (cast->value.type().bits() == value.type().bits() * 2)) {
                    if (const Call *inner_call = cast->value.as<Call>()) {
                        if (inner_call->name == "halide_xtensa_pad_to_native") {
                            is_narrowing = true;
                            value = inner_call->args[0];
                        }
                    }
                }
            }
        }
        if (call->name.find("halide_xtensa_sat_narrow_i") == 0) {
            is_sat_narrowing = true;
            value = call->args[0];
        }
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
        // TODO(vksnk): generalize this!
        int native_lanes = (64 / op->value.type().element_of().bytes());
        if (op->value.type().element_of().bytes() == 3) {
            native_lanes = 64;
        }
        if (op->value.type().element_of().bytes() == 6) {
            native_lanes = 32;
        }

        bool is_aligned_store = (op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0);
        if (external_buffers.count(op->name) > 0) {
            is_aligned_store = is_aligned_store && (op->param.host_alignment() % 64 == 0);
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
        const uint64_t *bits = as_const_uint(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type) && bits) {
            rhs << "IVP_SLLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type) && bits) {
            rhs << "IVP_SLLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) && bits) {
            rhs << "IVP_SLLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint8_t>(op->type)) {
                rhs << "IVP_SLL2NX8U(" << a0 << ", xb_vec2Nx8U_rtor_xb_vec2Nx8(" << a1 << "))";
            } else if (is_native_xtensa_vector<int8_t>(op->type)) {
                rhs << "IVP_SLA2NX8(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
                rhs << "IVP_SLLNX16U(" << a0 << ", xb_vecNx16U_rtor_xb_vecNx16(" << a1 << "))";
            } else if (is_native_xtensa_vector<int16_t>(op->type)) {
                rhs << "IVP_SLANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
                rhs << "IVP_SLLN_2X32U(" << a0 << ", xb_vecN_2x32Uv_rtor_xb_vecN_2x32v( " << a1 << "))";
            } else if (is_native_xtensa_vector<int32_t>(op->type)) {
                rhs << "IVP_SLAN_2X32(" << a0 << ", " << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    string a0 = print_expr(op->args[0]);
                    string a1 = print_expr(op->args[1]);
                    rhs << a0 << " << " << a1;
                } else {
                    rhs << print_expr(lower_signed_shift_left(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        const uint64_t *bits = as_const_uint(op->args[1]);
        if (is_native_xtensa_vector<uint8_t>(op->type) && bits) {
            rhs << "IVP_SRLI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<int8_t>(op->type) && bits) {
            rhs << "IVP_SRAI2NX8U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint16_t>(op->type) && bits) {
            rhs << "IVP_SRLINX16U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else if (is_native_xtensa_vector<uint32_t>(op->type) && bits) {
            rhs << "IVP_SRLIN_2X32U(" << a0 << ", " << std::to_string(*bits) << ")";
        } else {
            string a1 = print_expr(op->args[1]);
            if (is_native_xtensa_vector<uint8_t>(op->type)) {
                rhs << "IVP_SRL2NX8(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int8_t>(op->type)) {
                rhs << "IVP_SRA2NX8(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint16_t>(op->type)) {
                rhs << "IVP_SRLNX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int16_t>(op->type)) {
                rhs << "IVP_SRANX16(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<uint32_t>(op->type)) {
                rhs << "IVP_SRLN_2X32(" << a0 << ", " << a1 << ")";
            } else if (is_native_xtensa_vector<int32_t>(op->type)) {
                rhs << "IVP_SRAN_2X32(" << a0 << ", (int32x16_t)" << a1 << ")";
            } else {
                if (op->args[1].type().is_uint()) {
                    string a0 = print_expr(op->args[0]);
                    string a1 = print_expr(op->args[1]);
                    rhs << a0 << " >> " << a1;
                } else {
                    rhs << print_expr(lower_signed_shift_right(op->args[0], op->args[1]));
                }
            }
        }
    } else if (op->is_intrinsic(Call::count_leading_zeros)) {
        internal_assert(op->args.size() == 1);
        if (is_native_xtensa_vector<int16_t>(op->type) || is_native_xtensa_vector<uint16_t>(op->type)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUNX16(" : "xb_vecNx16_rtor_xb_vecNx16U(IVP_NSAUNX16U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (is_native_xtensa_vector<int32_t>(op->type) || is_native_xtensa_vector<uint32_t>(op->type)) {
            // TODO(vksnk): it seems that what Halide does is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "(IVP_NSAUN_2X32(" : "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(IVP_NSAUN_2X32U(";
            rhs << intrins_name << print_expr(op->args[0]) << "))";
        } else if (op->args[0].type().is_vector()) {
            rhs << print_type(op->type) << "::count_leading_zeros(" << print_expr(op->args[0]) << ")";
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_error << "Prefetch is not supported by Xtensa backend." << Expr(op) << "\n";
    } else if (op->name == "sqrt_f32") {
        string a0 = print_expr(op->args[0]);
        rhs << "sqrtf(" << a0 << ")";
    } else if (op->name == "round_f32") {
        string a0 = print_expr(op->args[0]);
        rhs << "roundf(" << a0 << ")";
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
    if ((is_native_xtensa_vector<int16_t>(t) || is_native_xtensa_vector<uint16_t>(t)) && (is_native_xtensa_vector<int16_t>(e.type()) || is_native_xtensa_vector<uint16_t>(e.type()))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecNx16_rtor_xb_vecNx16U(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecNx16U_rtor_xb_vecNx16(" + value + ")");
        }
    } else if ((is_native_xtensa_vector<int32_t>(t) || is_native_xtensa_vector<uint32_t>(t)) && (is_native_xtensa_vector<int32_t>(e.type()) || is_native_xtensa_vector<uint32_t>(e.type()))) {
        if (e.type().is_int()) {
            id = print_assignment(t, "xb_vecN_2x32v_rtor_xb_vecN_2x32Uv(" + value + ")");
        } else {
            id = print_assignment(t, "xb_vecN_2x32Uv_rtor_xb_vecN_2x32v(" + value + ")");
        }
    } else if (t.is_vector() &&
               t.lanes() == e.type().lanes() &&
               t != e.type()) {
        id = print_assignment(t, "convert_to_" + type + "_from_" + print_type(e.type()) + "(" + value + ")");
    } else {
        id = print_assignment(t, "(" + type + ")(" + value + ")");
    }
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

    // NOTE(vksnk): poor man's profiling below.
    // if (current_loop_level == 1) {
    //     open_scope();
    //     stream << get_indent() << "int cycles_start, cycles_stop, cyclesAV; (void)cycles_stop; (void)cyclesAV;\n";
    //     stream << get_indent() << "cycles_start = GetCycleCount();\n";
    // }
    // if (current_loop_level == 1) {
    //     stream << get_indent() << "cycles_start = GetCycleCount();\n";
    // }

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
    // NOTE(vksnk): Second part of the poor man's profiling below.
    // if (current_loop_level == 1) {
    //     stream << get_indent() << "cycles_stop = GetCycleCount();\n";
    //     stream << get_indent() << "cyclesAV = cycles_stop - cycles_start;\n";
    //     stream << get_indent() << "printf(\"" << op->name << ": %d\\n\", cyclesAV);\n";
    // }
    // if (current_loop_level == 1) {
    //     close_scope("profiler" + print_name(op->name));
    // }
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
    if (op->is_interleave() && (is_native_vector_type(op->vectors[0].type()) || (op->vectors[0].type().is_bool() && op->vectors[0].type().lanes() == 64))) {
        string type_suffix = suffix_for_type(op->type);

        Expr call = Call::make(op->type, "halide_xtensa_interleave" + type_suffix,
                               op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    if (op->is_slice() && (op->slice_stride() == 1) && (is_native_xtensa_vector<int8_t>(op->type) || is_native_xtensa_vector<uint8_t>(op->type) || is_native_xtensa_vector<int16_t>(op->type) || is_native_xtensa_vector<uint16_t>(op->type) || is_native_xtensa_vector<int32_t>(op->type) || is_native_xtensa_vector<uint32_t>(op->type) || is_native_xtensa_vector<float>(op->type))) {
        string type_suffix = suffix_for_type(op->type);
        string function_name = "halide_xtensa_slice";
        int slice_begin = op->slice_begin();
        if (op->slice_begin() < 5) {
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

    if (op->vectors.size() == 1 && is_double_native_vector_type(op->vectors[0].type())) {
        if (op->is_slice() && (op->slice_begin() < 2) && (op->slice_stride() == 2) && ((int)op->indices.size() == op->vectors[0].type().lanes() / 2)) {
            string type_suffix = suffix_for_type(op->type);
            string function_name = std::string("halide_xtensa_deinterleave") + ((op->slice_begin() == 0) ? "_even" : "_odd");
            Expr call = Call::make(op->type, function_name + type_suffix,
                                   {op->vectors[0]}, Call::PureExtern);
            call.accept(this);
            return;
        }
    }

    if (op->is_concat() && is_native_vector_type(op->vectors[0].type())) {
        Expr call = Call::make(op->type, "halide_xtensa_concat_from_native", op->vectors, Call::PureExtern);
        call.accept(this);
        return;
    }

    std::vector<string> vecs;
    for (Expr v : op->vectors) {
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
                    op->memory_type == MemoryType::Register ||
                    (op->memory_type == MemoryType::Auto &&
                     can_allocation_fit_on_stack(stack_bytes))) {
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
            stream << get_indent() << "if (("
                   << size_id << " > ((int64_t(1) << 31) - 1)) || (("
                   << size_id << " * sizeof("
                   << op_type << ")) > ((int64_t(1) << 31) - 1)))\n";
            open_scope();
            stream << get_indent();
            // TODO: call halide_error_buffer_allocation_too_large() here instead
            // TODO: call create_assertion() so that NoAssertions works
            stream << "halide_error(_ucon, "
                   << "\"32-bit signed overflow computing size of allocation " << op->name << "\\n\");\n";
            stream << get_indent() << "return -1;\n";
            close_scope("overflow test " + op->name);
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
            stream << "__attribute__((aligned(64))) " << op_name
                   << "[" << size_id << "];\n";
        } else if (op->memory_type == MemoryType::VTCM) {
            stream << "*"
                   << "__attribute__((aligned(64))) "
                   << " __restrict "
                   << op_name
                   << " = ("
                   << op_type
                   << " *)halide_tcm_malloc(_ucon, sizeof("
                   << op_type
                   << ")*" << size_id << ");\n";
        } else {
            stream << "*"
                   << "__attribute__((aligned(64)))  "
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
        create_assertion(op_name, Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern));

        string free_function = op->free_function.empty() ?
                                   (op->memory_type != MemoryType::VTCM ? "halide_free" : "halide_tcm_free") :
                                   op->free_function;

        if (op->memory_type != MemoryType::VTCM) {
        }

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

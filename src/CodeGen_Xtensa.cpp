#include "CodeGen_Xtensa.h"

#include <string>

#include "CodeGen_Internal.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Simplify.h"
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
void CodeGen_Xtensa::compile(const LoweredFunc &f) {
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

    // Emit the function prototype
    if (f.linkage == LinkageType::Internal) {
        // If the function isn't public, mark it static.
        stream << "static ";
    }
    stream << "HALIDE_FUNCTION_ATTRS\n";
    stream << "int " << simple_name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
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

            // Emit the body
            Stmt body = f.body;
            body = match_xtensa_patterns(body);
            //debug(0) << body;
            print(body);
            // stream << get_indent() << "printf(\"C code executed\\n\");";

            // Return success.
            stream << get_indent() << "return 0;\n";
        }

        indent -= 1;
        stream << "}\n";
    }

    if (is_header_or_extern_decl() && f.linkage == LinkageType::ExternalPlusMetadata) {
        // Emit the argv version
        stream << "\nHALIDE_FUNCTION_ATTRS\nint " << simple_name << "_argv(void **args);\n";

        // And also the metadata.
        stream << "\nHALIDE_FUNCTION_ATTRS\nconst struct halide_filter_metadata_t *" << simple_name << "_metadata();\n";
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

typedef xb_vecNx8 int8x64_t;
typedef xb_vec2Nx8 int8x128_t;
typedef xb_vecNx8U uint8x64_t;
typedef xb_vec2Nx8U uint8x128_t;
typedef xb_vecNx16 int16x32_t;
typedef xb_vecNx16U uint16x32_t;
typedef xb_vecN_2x32v int32x16_t;
typedef xb_vecN_2x32Uv uint32x16_t;
typedef xb_vecNx48 int48x32_t;
typedef vboolN_2 uint1x16_t;
typedef vboolN uint1x32_t;
typedef vbool2N uint1x64_t;

class int32x32_t {
  typedef int32x32_t Vec;
  typedef int32_t ElementType;
  typedef xb_vecN_2x32v CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;

public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline int32x32_t(Empty) {}

    enum FromCppVector { from_native_vector };
    inline int32x32_t(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v, v);
    }

    static Vec aligned_load(const void *base, int32_t offset) {
        xb_vec2Nx8 nv8_0, nv8_1;
        xb_vec2Nx8* ptr = (xb_vec2Nx8*)((const ElementType*)base + offset);
        IVP_L2U2NX8_XP(nv8_0, ptr, 0);
        ptr++;
        IVP_L2U2NX8_XP(nv8_1, ptr, 0);
        return Vec(from_native_vector,
                    IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(nv8_0)),
                    IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(nv8_1)));
    }

    static Vec load(const void *base, int32_t offset) {
        xb_vec2Nx8 nv8_0, nv8_1;
        xb_vec2Nx8* ptr = (xb_vec2Nx8*)((const ElementType*)base + offset);
        IVP_L2U2NX8_XP(nv8_0, ptr, 0);
        ptr++;
        IVP_L2U2NX8_XP(nv8_1, ptr, 0);
        return Vec(from_native_vector,
                    IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(nv8_0)),
                    IVP_MOVN_2X32_FROMNX16(IVP_MOVNX16_FROM2NX8(nv8_1)));
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    static Vec ramp(const ElementType &base, const ElementType &stride) {
        CppVectorType one_to_n = IVP_SEQN_2X32();
        CppVectorType base_w = base;
        CppVectorType stride_w = stride;
        CppVectorType lanes_2 = Lanes / 2;
        return Vec(from_native_vector,
                    base_w + IVP_PACKLN_2X64W(one_to_n * stride_w),
                    base_w + IVP_PACKLN_2X64W((lanes_2 + one_to_n) * stride_w));
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] + b.native_vector[0], a.native_vector[1] + b.native_vector[1]);
    }

    friend Vec operator-(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] - b.native_vector[0], a.native_vector[1] - b.native_vector[1]);
    }

    friend Vec operator*(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_PACKLN_2X64W(a.native_vector[0] * b.native_vector[0]),
                    IVP_PACKLN_2X64W(a.native_vector[1] * b.native_vector[1]));
    }

    friend Vec operator&(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                      a.native_vector[0] & b.native_vector[0],
                      a.native_vector[1] & b.native_vector[1]);
    }

    template <typename OtherVec>
    friend Vec operator>>(const Vec &a, const OtherVec &b) {
        return Vec(from_native_vector, a.native_vector[0] >> xb_vecN_2x32v(b.native_vector[0]),
                                       a.native_vector[1] >> xb_vecN_2x32v(b.native_vector[1]));
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return IVP_JOINBN_2(
                    IVP_LTN_2X32(a.native_vector[1], b.native_vector[1]),
                    IVP_LTN_2X32(a.native_vector[0], b.native_vector[0]));
    }

    friend Mask operator<=(const Vec &a, const Vec &b) {
        return IVP_JOINBN_2(
                    IVP_LEN_2X32(a.native_vector[1], b.native_vector[1]),
                    IVP_LEN_2X32(a.native_vector[0], b.native_vector[0]));
    }

    friend Mask operator==(const Vec &a, const Vec &b) {
        return IVP_JOINBN_2(
                    IVP_EQN_2X32(a.native_vector[1], b.native_vector[1]),
                    IVP_EQN_2X32(a.native_vector[0], b.native_vector[0]));
    }

    static Vec select(const Mask &cond, const Vec &true_value, const Vec &false_value) {
        return Vec(from_native_vector,
                    IVP_MOVN_2X32T(true_value.native_vector[0], false_value.native_vector[0], IVP_EXTRACTBLN(cond)),
                    IVP_MOVN_2X32T(true_value.native_vector[1], false_value.native_vector[1], IVP_EXTRACTBHN(cond)));
    }

    static Vec max(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_MAXN_2X32(a.native_vector[0], b.native_vector[0]),
                    IVP_MAXN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    // TODO: this should be improved by taking advantage of native operator support.
    static Vec min(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_MINN_2X32(a.native_vector[0], b.native_vector[0]),
                    IVP_MINN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    static Vec count_leading_zeros(const Vec &a) {
        return Vec(from_native_vector, IVP_NSAUN_2X32(a.native_vector[0]), IVP_NSAUN_2X32(a.native_vector[1]));
    }
};

class uint32x32_t {
  typedef uint32x32_t Vec;
  typedef uint32_t ElementType;
  typedef xb_vecN_2x32Uv CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;

  public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline uint32x32_t(Empty) {}

    enum FromCppVector { from_native_vector };
    inline uint32x32_t(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v, v);
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] + b.native_vector[0], a.native_vector[1] + b.native_vector[1]);
    }

    friend Vec operator*(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_PACKLN_2X64W(IVP_MULN_2X32(a.native_vector[0], b.native_vector[0])),
                    IVP_PACKLN_2X64W(IVP_MULN_2X32(a.native_vector[1], b.native_vector[1])));
    }

    friend Vec operator<<(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SLLN_2X32(a.native_vector[0], b.native_vector[0]),
                                       IVP_SLLN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    friend Vec operator>>(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SRLN_2X32(a.native_vector[0], b.native_vector[0]),
                                       IVP_SRLN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return IVP_JOINBN_2(
                    a.native_vector[1] < b.native_vector[1],
                    a.native_vector[0] < b.native_vector[0]);
    }

    static Vec max(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_MAXUN_2X32(a.native_vector[0], b.native_vector[0]),
                    IVP_MAXUN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    // TODO: this should be improved by taking advantage of native operator support.
    static Vec min(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_MINUN_2X32(a.native_vector[0], b.native_vector[0]),
                    IVP_MINUN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    static Vec count_leading_zeros(const Vec &a) {
        return Vec(from_native_vector, IVP_NSAUN_2X32(a.native_vector[0]), IVP_NSAUN_2X32(a.native_vector[1]));
    }
};

class int16x64_t {
  typedef int16_t ElementType;
  typedef xb_vecNx16 CppVectorType;
  static const int Lanes = 64;
public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline int16x64_t(Empty) {}

    enum FromCppVector { from_native_vector };
    inline int16x64_t(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

   static int16x64_t load(const void *base, int32_t offset) {
        int16x64_t r(empty);
        memcpy(&r.native_vector[0], ((const ElementType*)base + offset), sizeof(ElementType) * Lanes);
        return r;
    }

   static int16x64_t concat(const int16x32_t& a, const int16x32_t& b) {
        return int16x64_t(from_native_vector, a, b);
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }
};

class uint16x64_t {
  typedef uint16_t ElementType;
  typedef xb_vecNx16U CppVectorType;
  static const int Lanes = 64;
public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline uint16x64_t(Empty) {}

    enum FromCppVector { from_native_vector };
    inline uint16x64_t(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

   static uint16x64_t load(const void *base, int32_t offset) {
        uint16x64_t r(empty);
        memcpy(&r.native_vector[0], ((const ElementType*)base + offset), sizeof(ElementType) * Lanes);
        return r;
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }
};

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int8x64_t int8x64_t_aligned_load(const void *base, int32_t offset) {
    return *((const int8x64_t *)((int8_t*)base + offset));
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x128_t uint8x128_t_aligned_load(const void *base, int32_t offset) {
    return *((const uint8x128_t *)((uint8_t*)base + offset));
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x64_t uint8x64_t_aligned_load(const void *base, int32_t offset) {
    return *((const uint8x64_t *)((uint8_t*)base + offset));
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x64_t int16x64_t_aligned_load(const void *base, int32_t offset) {
    return *((const int16x64_t *)((int16_t*)base + offset));
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x32_t int16x32_t_aligned_load(const void *base, int32_t offset) {
    return *((const int16x32_t *)((int16_t*)base + offset));
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint8x64_t uint8x64_t_load(const void *base, int32_t offset) {
    uint8x64_t r;
    xb_vecNx8* ptr = (xb_vecNx8*)((const uint8_t*)base + offset);
    IVP_L2UNX8_XP(r, ptr, 0);
    return r;
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x32_t int16x32_t_load(const void *base, int32_t offset) {
    int16x32_t r;
    xb_vecNx16* ptr = (xb_vecNx16*)((const int16_t*)base + offset);
    IVP_L2UNX16_XP(r, ptr, 0);
    return r;
}

HALIDE_ALWAYS_INLINE int16x32_t int16x32_t_load(const void *base, const int32x32_t& offset) {
    int16_t tmp[32];
    int offsets[32];
    offset.store(&offsets[0], 0);
    for (int i = 0; i < 32; i++) {
        tmp[i] = ((const int16_t*)base)[offsets[i]];
    }

    return *((int16x32_t*)tmp);
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint16x32_t uint16x32_t_aligned_load(const void *base, int32_t offset) {
    return *((const uint16x32_t *)((uint16_t*)base + offset));
}

HALIDE_ALWAYS_INLINE uint16x32_t uint16x32_t_load(const void *base, const int32x32_t& offset) {
    uint16_t tmp[32];
    int offsets[32];
    offset.store(&offsets[0], 0);
    for (int i = 0; i < 32; i++) {
        tmp[i] = ((const uint16_t*)base)[offsets[i]];
    }

    return *((uint16x32_t*)tmp);
}

HALIDE_ALWAYS_INLINE void aligned_store(const uint8x64_t& a, void *base, int32_t offset) {
    *((uint8x64_t *)((uint8_t*)base + offset)) = a;
}

HALIDE_ALWAYS_INLINE void aligned_store(const int16x32_t& a, void *base, int32_t offset) {
    *((int16x32_t *)((int16_t*)base + offset)) = a;
}

HALIDE_ALWAYS_INLINE void store(const int16x32_t& a, void *base, int32_t offset) {
    //memcpy(((int16_t*)base + offset), &a, sizeof(int16_t) * 32);
    //TODO(vksnk): this seems to be right based on their doc, but double-check
    valign align;
    xb_vecNx16* ptr = (xb_vecNx16*)((const int16_t*)base + offset);
    IVP_SANX16_IP(a, align, ptr);
    // Flush alignment register.
    IVP_SAPOS_FP(align, (xb_vec2Nx8*)ptr);
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint16x32_t uint16x32_t_load(const void *base, int32_t offset) {
    uint16x32_t r;
    uint16x32_t* ptr = (uint16x32_t*)((const int16_t*)base + offset);
    IVP_L2UNX16U_XP(r, ptr, 0);
    return r;
}

HALIDE_ALWAYS_INLINE void aligned_store(const uint16x32_t& a, void *base, int32_t offset) {
    *((uint16x32_t *)((uint16_t*)base + offset)) = a;
}

HALIDE_ALWAYS_INLINE void store(const uint16x32_t& a, void *base, int32_t offset) {
    memcpy(((uint16_t*)base + offset), &a, sizeof(uint16_t) * 32);
}

HALIDE_ALWAYS_INLINE void aligned_store(const int16x64_t& a, void *base, int32_t offset) {
   //a.aligned_store(base, offset);
   xb_vecNx16 * ptr = (int16x32_t *)((int16_t*)base + offset);
   ptr[0] = a.native_vector[0];
   ptr[1] = a.native_vector[1];
}

HALIDE_ALWAYS_INLINE void store(const int16x64_t& a, void *base, int32_t offset) {
  a.store(base, offset);
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x16_t int32x16_t_load(const void *base, int32_t offset) {
    int32x16_t r;
    memcpy(&r, ((const int32_t*)base + offset), sizeof(int32_t) * 16);
    return r;
}

HALIDE_ALWAYS_INLINE void aligned_store(const int32x16_t& a, void *base, int32_t offset) {
    *((int32x16_t *)((int32_t*)base + offset)) = a;
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED uint32x16_t uint32x16_t_load(const void *base, int32_t offset) {
    uint32x16_t r;
    memcpy(&r, ((const uint32_t*)base + offset), sizeof(uint32_t) * 16);
    return r;
}

HALIDE_ALWAYS_INLINE void aligned_store(const uint32x16_t& a, void *base, int32_t offset) {
    *((uint32x16_t *)((uint32_t*)base + offset)) = a;
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t int32x32_t_aligned_load(const void *base, int32_t offset) {
    return int32x32_t::aligned_load(base, offset);
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int32x32_t int32x32_t_load(const void *base, int32_t offset) {
    return int32x32_t::load(base, offset);
}

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x64_t int16x64_t_load(const void *base, int32_t offset) {
    return int16x64_t::load(base, offset);
}

HALIDE_ALWAYS_INLINE void aligned_store(const int32x32_t& a, void *base, int32_t offset) {
   a.aligned_store(base, offset);
}

HALIDE_ALWAYS_INLINE void store(const int32x32_t& a, void *base, int32_t offset) {
  a.store(base, offset);
}

HALIDE_ALWAYS_INLINE void aligned_store(const uint32x32_t& a, void *base, int32_t offset) {
   a.aligned_store(base, offset);
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_interleave_i16(const int16x32_t& a, const int16x32_t& b) {
  return int16x64_t(int16x64_t::from_native_vector,
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_deinterleave_even_i16(const int16x64_t& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_0);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_deinterleave_odd_i16(const int16x64_t& a) {
  return  IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_EXTRACT_1_OF_2_OFF_1);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_start_1_i16(const int16x64_t& a) {
  return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_ROTATE_RIGHT_1);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_start_2_i16(const int16x64_t& a) {
  return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_ROTATE_RIGHT_2);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_start_3_i16(const int16x64_t& a) {
  return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_ROTATE_RIGHT_3);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_start_4_i16(const int16x64_t& a) {
  return IVP_SELNX16I(a.native_vector[1], a.native_vector[0], IVP_SELI_16B_ROTATE_RIGHT_4);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_i16(const int16x64_t& a, int start) {
  return IVP_SELNX16 (a.native_vector[1], a.native_vector[0], IVP_SEQNX16() + int16x32_t(start));
}

HALIDE_ALWAYS_INLINE uint8x128_t halide_xtensa_dynamic_shuffle(const uint8x128_t& a, const int8x128_t& b, int min_range, int max_range) {
  return IVP_SHFL2NX8U(a, b);
}

HALIDE_ALWAYS_INLINE uint8x64_t halide_xtensa_dynamic_shuffle(const uint8x64_t& a, const int8x64_t& b, int min_range, int max_range) {
  return IVP_SHFL2NX8(a, b);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_dynamic_shuffle(const int16x32_t& a, const int16x32_t& b, int min_range, int max_range) {
  return IVP_SHFLNX16(a, b);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_dynamic_shuffle(const int16x64_t& a, const int16x32_t& b, int min_range, int max_range) {
  return IVP_SELNX16(a.native_vector[1], a.native_vector[0], b);
}

HALIDE_ALWAYS_INLINE uint16x32_t uint16x32_t_shift_right(const uint16x32_t &a, const uint16x32_t &b) {
    return IVP_SRLNX16(a, b);
}

HALIDE_ALWAYS_INLINE uint32x16_t uint32x16_t_shift_right(const uint32x16_t &a, const uint32x16_t &b) {
    return IVP_SRLN_2X32(a, b);
}

HALIDE_ALWAYS_INLINE uint16x32_t uint16x32_t_shift_left(const uint16x32_t &a, const uint16x32_t &b) {
    return IVP_SLLNX16(a, b);
}

HALIDE_ALWAYS_INLINE uint32x16_t uint32x16_t_shift_left(const uint32x16_t &a, const uint32x16_t &b) {
    return IVP_SLLN_2X32(a, b);
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_sat_add_i32(const int32x16_t& a,
                                                                      const int32x16_t& b) {
  // I am not 100% about it.
  xb_vecN_2x32v zero = 0;
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = a * one;
  IVP_MULAN_2X32(l0, b, one);
  return IVP_PACKVN_2X64W(l0, zero);
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

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_mul_i48(const int16x32_t& a, const int16x32_t& b) {
  return a * b;
}

HALIDE_ALWAYS_INLINE int32x32_t halide_xtensa_widen_mul_i32(const int16x32_t& a, const int16x32_t& b) {
  xb_vecNx48 r = a * b;
  return int32x32_t(int32x32_t::from_native_vector,
                                IVP_CVT32SNX48L(r),
                                IVP_CVT32SNX48H(r));
}

HALIDE_ALWAYS_INLINE uint32x32_t halide_xtensa_widen_mul_u32(const uint16x32_t& a,
                                                                         const uint16x32_t& b) {
  xb_vecNx48 r = a * b;
  return uint32x32_t(uint32x32_t::from_native_vector,
                                IVP_CVT32UNX48L(r),
                                IVP_CVT32UNX48H(r));
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_mul_add_i48(const int48x32_t& a, const int16x32_t& b, const int16x32_t& c) {
  int48x32_t r = a;
  IVP_MULANX16(r, b, c);
  return r;
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
  return IVP_ADDWUNX16(a, b);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_add_u48(const int48x32_t& a, const uint16x32_t& b) {
  int48x32_t r = a;
  IVP_ADDWUANX16(r, b, uint16x32_t(0));
  return r;
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_pair_add_u48(const int48x32_t& a, const uint16x32_t& b, const uint16x32_t& c) {
  int48x32_t r = a;
  IVP_ADDWUANX16(r, b, c);
  return r;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_narrow_i48x_with_shift_i16(const int48x32_t& a, int shift) {
  return IVP_PACKVRNRNX48(a, shift);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_narrow_i48x_with_shift_u16(const int48x32_t& a, int shift) {
  return IVP_PACKVRNRNX48(a, shift);
}

HALIDE_ALWAYS_INLINE int48x32_t halide_xtensa_widen_mul_u48(const uint16x32_t& a,
                                                                         const uint16x32_t& b) {
  return a * b;
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_narrow_with_shift_i16(const int32x32_t& a, int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return IVP_PACKVRNRNX48(wide, shift);
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

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_lerp_i16(const int16x32_t& a, const int16x32_t& b, uint16_t w) {
  // TODO(vksnk): Halide lerp actually uses full range, but it's not clear from the documentation
  // if we can pass unsigned type to IVP_MULPN16XR16, so just to be extra careful reduce it to 14-bit
  // for now.
  uint32_t w32 = ((uint32_t(w)) >> 2);
  uint32_t alphaMalpha = ((16384 - w32) << 16) | w32;
  xb_vecNx48 output = IVP_MULPN16XR16(a, b, alphaMalpha);
  return IVP_PACKVRNRNX48(output, 14);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_avg121_round_i16(const int16x32_t& a, const int16x32_t& b, const int16x32_t& c) {
  static const int16_t kCeilAvg121Coef[] = {1, 1, 2, 3};
  xb_int64pr * __restrict coef = (xb_int64pr*)kCeilAvg121Coef;
  xb_vecNx48 result = IVP_MULQN16XR16(xb_vecNx16(1), c, b, a, coef[0]);
  return IVP_PACKVRNRNX48(result, 2);
}

inline int16x64_t convert_to_int16x64_t_from_uint8x64_t(const uint8x64_t& src) {
  int16x64_t result = src;
  return result;
}

inline int8x64_t convert_to_int8x64_t_from_int16x64_t(const int16x64_t& src) {
  int8x64_t result = src;
  return result;
}

inline uint8x64_t convert_to_uint8x64_t_from_int16x64_t(const int16x64_t& src) {
  uint8x64_t result = src;
  return result;
}

inline uint16x64_t convert_to_uint16x64_t_from_uint8x64_t(const uint8x64_t& src) {
  uint16x64_t result = src;
  return result;
}

inline int16x32_t convert_to_int16x32_t_from_int32x32_t(const int32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

inline int16x32_t convert_to_int16x32_t_from_uint32x32_t(const uint32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

inline uint16x32_t convert_to_uint16x32_t_from_int32x32_t(const int32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

inline uint16x32_t convert_to_uint16x32_t_from_uint32x32_t(const uint32x32_t& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return IVP_PACKLNX48(wide);
}

inline int32x32_t convert_to_int32x32_t_from_int16x32_t(const int16x32_t& src) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src);
    return int32x32_t(int32x32_t::from_native_vector,
                      IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
}

inline int32x32_t convert_to_int32x32_t_from_uint16x32_t(const uint16x32_t& src) {
    xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, src);
    return int32x32_t(int32x32_t::from_native_vector,
                      IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
}

inline int32x32_t convert_to_int32x32_t_from_uint32x32_t(const uint32x32_t& src) {
    return int32x32_t(int32x32_t::from_native_vector,
                      src.native_vector[0], src.native_vector[1]);
}

inline int32x32_t convert_to_int32x32_t_from_int48x32_t(const int48x32_t& src) {
    return int32x32_t(int32x32_t::from_native_vector,
                                IVP_CVT32SNX48L(src),
                                IVP_CVT32SNX48H(src));
}

inline uint32x32_t convert_to_uint32x32_t_from_uint16x32_t(const uint16x32_t& src) {
    xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, src);
    return uint32x32_t(uint32x32_t::from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
}

inline uint32x32_t convert_to_uint32x32_t_from_int48x32_t(const int48x32_t& src) {
    return uint32x32_t(uint32x32_t::from_native_vector,
                                IVP_CVT32UNX48L(src),
                                IVP_CVT32UNX48H(src));
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_to_native(const int16x64_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_slice_to_native(const int16x32_t& src, int index, int native_lanes, int total_lanes) {
  return src;
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_concat_from_native(const int16x32_t& a, const int16x32_t& b) {
    return int16x64_t(int16x64_t::from_native_vector, a, b);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_slice_to_native(const uint16x64_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_slice_to_native(const uint16x32_t& src, int index, int native_lanes, int total_lanes) {
  return src;
}

HALIDE_ALWAYS_INLINE uint16x64_t halide_xtensa_concat_from_native(const uint16x32_t& a, const uint16x32_t& b) {
    return uint16x64_t(uint16x64_t::from_native_vector, a, b);
}

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_slice_to_native(const int32x32_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
}

HALIDE_ALWAYS_INLINE int32x32_t halide_xtensa_concat_from_native(const int32x16_t& a, const int32x16_t& b) {
    return int32x32_t(int32x32_t::from_native_vector, a, b);
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_slice_to_native(const uint32x32_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
}

HALIDE_ALWAYS_INLINE uint1x16_t halide_xtensa_slice_to_native(const uint1x32_t& src, int index, int native_lanes, int total_lanes) {
  return (index == 0)?IVP_EXTRACTBLN(src):IVP_EXTRACTBHN(src);
}


HALIDE_ALWAYS_INLINE uint32x32_t halide_xtensa_concat_from_native(const uint32x16_t& a, const uint32x16_t& b) {
    return uint32x32_t(uint32x32_t::from_native_vector, a, b);
}

inline int32x16_t halide_xtensa_convert_i16_low_i32(const int16x32_t& src, int native_lanes, int total_lines) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src);
    return IVP_CVT32S2NX24LL(wide);
}

inline int32x16_t halide_xtensa_convert_i16_high_i32(const int16x32_t& src, int native_lanes, int total_lines) {
    xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src);
    return IVP_CVT32S2NX24LH(wide);
}

inline int32x16_t halide_xtensa_convert_i48_low_i32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32SNX48L(src);
}

inline int32x16_t halide_xtensa_convert_i48_high_i32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32SNX48H(src);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_concat_i32_to_i16(const int32x16_t& a, const int32x16_t& b) {
  xb_vecNx48 wide = IVP_CVT48SNX32(b, a);
  return IVP_PACKLNX48(wide);
}

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_convert_concat_u32_to_i16(const uint32x16_t& a, const uint32x16_t& b) {
  xb_vecNx48 wide = IVP_CVT48UNX32(b, a);
  return IVP_PACKLNX48(wide);
}

HALIDE_ALWAYS_INLINE uint16x32_t halide_xtensa_convert_concat_u32_to_u16(const uint32x16_t& a, const uint32x16_t& b) {
  xb_vecNx48 wide = IVP_CVT48UNX32(b, a);
  return IVP_PACKLNX48(wide);
}

inline uint32x16_t halide_xtensa_convert_i48_low_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32UNX48L(src);
}

inline uint32x16_t halide_xtensa_convert_i48_high_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32UNX48H(src);
}

HALIDE_ALWAYS_INLINE uint1x32_t halide_xtensa_concat_from_native(const uint1x16_t& a, const uint1x16_t& b) {
        return IVP_JOINBN_2(b, a);
}

)INLINE_CODE";

        // Vodoo fix: on at least one config (our arm32 buildbot running gcc 5.4),
        // emitting this long text string was regularly garbled in a predictable pattern;
        // flushing the stream before or after heals it. Since C++ codegen is rarely
        // on a compilation critical path, we'll just band-aid it in this way.
        stream << std::flush;
        stream << native_typedef_decl;
        stream << std::flush;
    }
}

bool CodeGen_Xtensa::is_native_vector_type(Type t) {
    if (t.is_int_or_uint() && (t.lanes() == 32) && (t.bits() == 16)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 16) && (t.bits() == 32)) {
        return true;
    }

    return false;
}

string CodeGen_Xtensa::print_cast_expr(const Type &t, const Expr &e) {
    string value = print_expr(e);
    string type = print_type(t);
    if (t.is_int_or_uint() && e.type().is_int_or_uint() &&
        (e.type().bits() == 16) && (e.type().lanes() == 32) &&
        (t.bits() == 16) && (t.lanes() == 32)) {
        return print_assignment(t, "(" + type + ")(" + value + ")");
    } else if (t.is_vector() &&
               t.lanes() == e.type().lanes() &&
               t != e.type()) {
        return print_assignment(t, "convert_to_" + type + "_from_" + print_type(e.type()) + "(" + value + ")");
    } else {
        return print_assignment(t, "(" + type + ")(" + value + ")");
    }
}

void CodeGen_Xtensa::visit(const Mul *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        if (op->type.is_uint() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "uint16x32_t_shift_left(" + sa + ", " + std::to_string(bits) + ")");
        } else if (op->type.is_uint() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "uint32x16_t_shift_left(" + sa + ", " + std::to_string(bits) + ")");
        } else {
            visit_binop(op->type, op->a, make_const(op->a.type(), bits), "<<");
        }
    } else {
        if (op->type.is_int() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_MULNX16PACKL(" + sa + ", " + sb + ")");
        } else if (op->type.is_int() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            string sa = print_expr(op->a);
            string sb = print_expr(op->b);
            print_assignment(op->type, "IVP_PACKLN_2X64W(" + sa + " * " + sb + ")");
        } else {
            visit_binop(op->type, op->a, op->b, "*");
        }
    }
}

string CodeGen_Xtensa::print_xtensa_call(const Call *op) {
    ostringstream rhs;
    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }

    // This is just multiplication.
    if (op->name == "halide_xtensa_widen_mul_i48") {
        internal_assert(args.size() == 2);
        rhs << "int16x32_t(" << args[0] + ") * int16x32_t(" + args[1] + ")";
        return rhs.str();
    }

    string op_name = op->name;
    if (op->name == "halide_xtensa_sat_add_i16") {
        op_name = "IVP_ADDSNX16";
    } else if (op->name == "halide_xtensa_sat_sub_i16") {
        op_name = "IVP_SUBSNX16";
    } else if (op->name == "halide_xtensa_avg_i16") {
        op_name = "IVP_AVGNX16";
    } else if (op->name == "halide_xtensa_avg_u16") {
        op_name = "IVP_AVGUNX16";
    } else if (op->name == "halide_xtensa_avg_round_i16") {
        op_name = "IVP_AVGRNX16";
    } else if (op->name == "halide_xtensa_avg_round_u16") {
        op_name = "IVP_AVGRUNX16";
    } else if (op->name == "halide_xtensa_absd_i16") {
        op_name = "IVP_ABSSUBNX16";
    } else if (op->name == "halide_xtensa_widen_pair_mul_u48") {
        op_name = "IVP_MULUUPNX16";
    } else if (op->name == "halide_xtensa_convert_i48_low_i32") {
        op_name = "IVP_CVT32SNX48L";
    } else if (op->name == "halide_xtensa_convert_i48_high_i32") {
        op_name = "IVP_CVT32SNX48H";
    } else if (op->name == "halide_xtensa_convert_i48_low_u32") {
        op_name = "IVP_CVT32UNX48L";
    } else if (op->name == "halide_xtensa_convert_i48_high_u32") {
        op_name = "IVP_CVT32UNX48H";
    } else if (op->name == "halide_xtensa_narrow_i48x_with_shift_u16") {
        op_name = "IVP_PACKVRNRNX48";
    }

    rhs << op_name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_Xtensa::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRLNX16(" + sa + ", " + std::to_string(bits) + ")");
        } else if (op->type.is_uint() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "IVP_SRLN_2X32(" + sa + ", " + std::to_string(bits) + ")");
        } else if (op->type.is_int() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, sa + " >> (int32x16_t)" + std::to_string(bits));
        } else {
            visit_binop(op->type, op->a, make_const(op->a.type(), bits), ">>");
        }
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_Xtensa::visit(const Max *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_max", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (op->type.is_int() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "IVP_MAXNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "IVP_MAXUNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_int() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << "IVP_MAXN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_uint() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << "IVP_MAXUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else {
            rhs << print_type(op->type) << "::max(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_Xtensa::visit(const Min *op) {
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_min", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        if (op->type.is_int() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "IVP_MINNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "IVP_MINUNX16(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_int() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << "IVP_MINN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        } else if (op->type.is_uint() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << "IVP_MINUN_2X32(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
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

    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
    if (op->condition.type().is_scalar()) {
        rhs << "(" << type << ")"
            << "(" << cond
            << " ? " << true_val
            << " : " << false_val
            << ")";
    } else {
        if (op->type.is_int() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            rhs << "IVP_MOVNX16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (op->type.is_uint() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            rhs << "IVP_MOVNX16UT(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (op->type.is_int() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            rhs << "IVP_MOVN_2X32T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else if (op->type.is_uint() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            rhs << "IVP_MOVN_2X32UT(" << true_val << ", " << false_val << ", " << cond << ")";
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
    if (op->type.is_int() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
        print_assignment(vector_type, "/* ramp */ int32x16_t(" + id_base + ") + IVP_PACKLN_2X64W(IVP_SEQN_2X32() * int32x16_t(" + id_stride + "))");
    } else {
        print_assignment(vector_type, print_type(vector_type) + "::ramp(" + id_base + ", " + id_stride + ")");
    }
}

void CodeGen_Xtensa::visit(const Broadcast *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_value = print_expr(op->value);
    string rhs;
    if (is_native_vector_type(op->type)) {
        rhs = print_type(vector_type) + "(" + id_value + ")";
    } else if (op->lanes > 1) {
        rhs = print_type(vector_type) + "::broadcast(" + id_value + ")";
    } else {
        rhs = id_value;
    }

    print_assignment(vector_type, rhs);
}

void CodeGen_Xtensa::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "Predicated load is not supported by C backend." << Expr(op) << "\n";

    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.
    ostringstream rhs;

    Type t = op->type;
    string name = print_name(op->name);

    // If we're loading a contiguous ramp into a vector, just load the vector
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (dense_ramp_base.defined()) {
        internal_assert(t.is_vector());
        std::string op_name;
        // TODO(vksnk): generalize this!
        int native_lanes = 64 / op->type.element_of().bytes();
        if ((op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0)) {
            op_name = "_aligned_load(";
            // debug(0) << "Aligned load\n";
        } else {
            op_name = "_load(";
            // debug(0) << "Unaligned load " << op->alignment.modulus << " " << op->alignment.remainder
            //     << " " << op->type.lanes() << "\n";
        }
        string id_ramp_base = print_expr(dense_ramp_base);
        rhs << print_type(t) + op_name << name << ", " << id_ramp_base << ")";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(t.is_vector());
        // debug(0) << "gather load " << op->index << "\n";
        string id_index = print_expr(op->index);
        rhs << print_type(t) + "_load(" << name << ", " << id_index << ")";
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
    user_assert(is_one(op->predicate)) << "Predicated store is not supported by C backend.\n";

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

    string id_value = print_expr(op->value);
    string name = print_name(op->name);

    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.

    // If we're writing a contiguous ramp, just store the vector.
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (dense_ramp_base.defined()) {
        internal_assert(op->value.type().is_vector());
        string op_name;
        // TODO(vksnk): generalize this!
        int native_lanes = 64 / op->value.type().element_of().bytes();
        if ((op->alignment.modulus % native_lanes == 0) && (op->alignment.remainder % native_lanes == 0)) {
            // debug(0) << "Aligned store\n";
            op_name = "aligned_store(";
        } else {
            // debug(0) << "Unaligned store " << op->alignment.modulus << " " << op->alignment.remainder
            //     << " " << op->value.type().lanes() << "\n";
            op_name = "store(";
        }

        string id_ramp_base = print_expr(dense_ramp_base);
        stream << get_indent() << op_name << id_value << ", " << name << ", " << id_ramp_base << ");\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());
        string id_index = print_expr(op->index);
        stream << get_indent() << id_value + ".store(" << name << ", " << id_index << ");\n";
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

    internal_assert(op->is_extern() || op->is_intrinsic())
        << "Can only codegen extern calls and intrinsics\n";

    ostringstream rhs;

    // Handle intrinsics first
    if (op->is_intrinsic(Call::debug_to_file)) {
        internal_assert(op->args.size() == 3);
        const StringImm *string_imm = op->args[0].as<StringImm>();
        internal_assert(string_imm);
        string filename = string_imm->value;
        string typecode = print_expr(op->args[1]);
        string buffer = print_name(print_expr(op->args[2]));

        rhs << "halide_debug_to_file(_ucon, "
            << "\"" << filename << "\", "
            << typecode
            << ", (struct halide_buffer_t *)" << buffer << ")";
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " & " << a1;
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " ^ " << a1;
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " | " << a1;
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        rhs << "~" << print_expr(op->args[0]);
    } else if (op->is_intrinsic(Call::reinterpret)) {
        internal_assert(op->args.size() == 1);
        rhs << print_reinterpret(op->type, op->args[0]);
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "uint16x32_t_shift_left(" << a0 << ", " << a1 << ")";
        } else if (op->type.is_uint() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << "uint32x16_t_shift_left(" << a0 << ", " << a1 << ")";
        } else {
            rhs << a0 << " << " << a1;
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            rhs << "IVP_SRLNX16(" << a0 << ", " << a1 << ")";
        } else if (op->type.is_int() && (op->type.lanes() == 16) && (op->type.bits() == 32)) {
            rhs << a0 << " >> (int32x16_t)" << a1;
        } else {
            rhs << a0 << " >> " << a1;
        }
    } else if (op->is_intrinsic(Call::count_leading_zeros)) {
        internal_assert(op->args.size() == 1);
        if (op->type.is_int_or_uint() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            // TODO(vksnk): it seems that what halide is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "IVP_NSAUNX16(" : "IVP_NSAUNX16(";
            rhs << intrins_name << print_expr(op->args[0]) << ")";
        } else if (op->type.is_int_or_uint() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            // TODO(vksnk): it seems that what halide is always matching IVP_NSAUN*?
            string intrins_name = op->type.is_int() ? "IVP_NSAUN_2X32(" : "IVP_NSAUN_2X32(";
            rhs << intrins_name << print_expr(op->args[0]) << ")";
        } else if (op->args[0].type().is_vector()) {
            rhs << print_type(op->type) << "::count_leading_zeros(" << print_expr(op->args[0]) << ")";
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (
        // op->is_intrinsic(Call::count_leading_zeros) ||
        op->is_intrinsic(Call::count_trailing_zeros) ||
        op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        if (op->args[0].type().is_vector()) {
            rhs << print_scalarized_expr(op);
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_assert(op->args.size() == 3);
        Expr e = lower_lerp(op->args[0], op->args[1], op->args[2]);
        rhs << "/*lerp = */" << print_expr(e);
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = cast(op->type, select(a < b, b - a, a - b));
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        string arg0 = print_expr(op->args[0]);
        string arg1 = print_expr(op->args[1]);
        rhs << "return_second(" << arg0 << ", " << arg1 << ")";
    } else if (op->is_intrinsic(Call::if_then_else)) {
        internal_assert(op->args.size() == 3);

        string result_id = unique_name('_');

        stream << get_indent() << print_type(op->args[1].type(), AppendSpace)
               << result_id << ";\n";

        string cond_id = print_expr(op->args[0]);

        stream << get_indent() << "if (" << cond_id << ")\n";
        open_scope();
        string true_case = print_expr(op->args[1]);
        stream << get_indent() << result_id << " = " << true_case << ";\n";
        close_scope("if " + cond_id);
        stream << get_indent() << "else\n";
        open_scope();
        string false_case = print_expr(op->args[2]);
        stream << get_indent() << result_id << " = " << false_case << ";\n";
        close_scope("if " + cond_id + " else");

        rhs << result_id;
    } else if (op->is_intrinsic(Call::require)) {
        internal_assert(op->args.size() == 3);
        if (op->args[0].type().is_vector()) {
            rhs << print_scalarized_expr(op);
        } else {
            create_assertion(op->args[0], op->args[2]);
            rhs << print_expr(op->args[1]);
        }
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        Expr a0 = op->args[0];
        rhs << "/*abs = */" << print_expr(cast(op->type, select(a0 > 0, a0, -a0)));
    } else if (op->is_intrinsic(Call::memoize_expr)) {
        internal_assert(!op->args.empty());
        string arg = print_expr(op->args[0]);
        rhs << "(" << arg << ")";
    } else if (op->is_intrinsic(Call::alloca)) {
        internal_assert(op->args.size() == 1);
        internal_assert(op->type.is_handle());
        const Call *call = op->args[0].as<Call>();
        if (op->type == type_of<struct halide_buffer_t *>() &&
            call && call->is_intrinsic(Call::size_of_halide_buffer_t)) {
            stream << get_indent();
            string buf_name = unique_name('b');
            stream << "halide_buffer_t " << buf_name << ";\n";
            rhs << "&" << buf_name;
        } else {
            // Make a stack of uint64_ts
            string size = print_expr(simplify((op->args[0] + 7) / 8));
            stream << get_indent();
            string array_name = unique_name('a');
            stream << "uint64_t " << array_name << "[" << size << "];";
            rhs << "(" << print_type(op->type) << ")(&" << array_name << ")";
        }
    } else if (op->is_intrinsic(Call::make_struct)) {
        if (op->args.empty()) {
            internal_assert(op->type.handle_type);
            // Add explicit cast so that different structs can't cache to the same value
            rhs << "(" << print_type(op->type) << ")(NULL)";
        } else if (op->type == type_of<halide_dimension_t *>()) {
            // Emit a shape

            // Get the args
            vector<string> values;
            for (size_t i = 0; i < op->args.size(); i++) {
                values.push_back(print_expr(op->args[i]));
            }

            static_assert(sizeof(halide_dimension_t) == 4 * sizeof(int32_t),
                          "CodeGen_C assumes a halide_dimension_t is four densely-packed int32_ts");

            internal_assert(values.size() % 4 == 0);
            int dimension = values.size() / 4;

            string shape_name = unique_name('s');
            stream
                << get_indent() << "struct halide_dimension_t " << shape_name
                << "[" << dimension << "];\n";
            // indent++;
            for (int i = 0; i < dimension; i++) {
                stream
                    // << get_indent() << "{"
                    << get_indent() << shape_name << "[" << i << "].min = " << values[i * 4 + 0] << ";\n"
                    << get_indent() << shape_name << "[" << i << "].extent = " << values[i * 4 + 1] << ";\n"
                    << get_indent() << shape_name << "[" << i << "].stride = " << values[i * 4 + 2] << ";\n"
                    << get_indent() << shape_name << "[" << i << "].flags = " << values[i * 4 + 3] << ";\n";
            }
            // indent--;
            // stream << get_indent() << "};\n";

            rhs << shape_name;
        } else {
            // Emit a declaration like:
            // struct {const int f_0, const char f_1, const int f_2} foo = {3, 'c', 4};

            // Get the args
            vector<string> values;
            for (size_t i = 0; i < op->args.size(); i++) {
                values.push_back(print_expr(op->args[i]));
            }
            stream << get_indent() << "struct {\n";
            // List the types.
            indent++;
            for (size_t i = 0; i < op->args.size(); i++) {
                stream << get_indent() << "const " << print_type(op->args[i].type()) << " f_" << i << ";\n";
            }
            indent--;
            string struct_name = unique_name('s');
            stream << get_indent() << "} " << struct_name << " = {\n";
            // List the values.
            indent++;
            for (size_t i = 0; i < op->args.size(); i++) {
                stream << get_indent() << values[i];
                if (i < op->args.size() - 1) stream << ",";
                stream << "\n";
            }
            indent--;
            stream << get_indent() << "};\n";

            // Return a pointer to it of the appropriate type

            // TODO: This is dubious type-punning. We really need to
            // find a better way to do this. We dodge the problem for
            // the specific case of buffer shapes in the case above.
            if (op->type.handle_type) {
                rhs << "(" << print_type(op->type) << ")";
            }
            rhs << "(&" << struct_name << ")";
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        // Rewrite to an snprintf
        vector<string> printf_args;
        string format_string = "";
        for (size_t i = 0; i < op->args.size(); i++) {
            Type t = op->args[i].type();
            printf_args.push_back(print_expr(op->args[i]));
            if (t.is_int()) {
                format_string += "%lld";
                printf_args[i] = "(long long)(" + printf_args[i] + ")";
            } else if (t.is_uint()) {
                format_string += "%llu";
                printf_args[i] = "(long long unsigned)(" + printf_args[i] + ")";
            } else if (t.is_float()) {
                if (t.bits() == 32) {
                    format_string += "%f";
                } else {
                    format_string += "%e";
                }
            } else if (op->args[i].as<StringImm>()) {
                format_string += "%s";
            } else {
                internal_assert(t.is_handle());
                format_string += "%p";
            }
        }
        string buf_name = unique_name('b');
        stream << get_indent() << "char " << buf_name << "[1024];\n";
        stream << get_indent() << "snprintf(" << buf_name << ", 1024, \"" << format_string << "\", " << with_commas(printf_args) << ");\n";
        rhs << buf_name;

    } else if (op->is_intrinsic(Call::register_destructor)) {
        internal_assert(op->args.size() == 2);
        const StringImm *fn = op->args[0].as<StringImm>();
        internal_assert(fn);
        string arg = print_expr(op->args[1]);

        stream << get_indent();
        // Make a struct on the stack that calls the given function as a destructor
        string struct_name = unique_name('s');
        string instance_name = unique_name('d');
        stream << "struct " << struct_name << " { "
               << "void * const ucon; "
               << "void * const arg; "
               << "" << struct_name << "(void *ucon, void *a) : ucon(ucon), arg((void *)a) {} "
               << "~" << struct_name << "() { " << fn->value + "(ucon, arg); } "
               << "} " << instance_name << "(_ucon, " << arg << ");\n";
        rhs << print_expr(0);
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " / " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " % " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::signed_integer_overflow)) {
        user_error << "Signed integer overflow occurred during constant-folding. Signed"
                      " integer overflow for int32 and int64 is undefined behavior in"
                      " Halide.\n";
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_assert((op->args.size() == 4) && is_one(op->args[2]))
            << "Only prefetch of 1 cache line is supported in C backend.\n";
        const Variable *base = op->args[0].as<Variable>();
        internal_assert(base && base->type.is_handle());
        rhs << "__builtin_prefetch("
            << "((" << print_type(op->type) << " *)" << print_name(base->name)
            << " + " << print_expr(op->args[1]) << "), 1)";
    } else if (op->is_intrinsic(Call::size_of_halide_buffer_t)) {
        rhs << "(sizeof(halide_buffer_t))";
    } else if (op->is_intrinsic(Call::strict_float)) {
        internal_assert(op->args.size() == 1);
        string arg0 = print_expr(op->args[0]);
        rhs << "(" << arg0 << ")";
    } else if (op->is_intrinsic()) {
        // TODO: other intrinsics
        internal_error << "Unhandled intrinsic in C backend: " << op->name << "\n";
    } else if (op->name.find("halide_xtensa_") == 0) {
        rhs << print_xtensa_call(op);
    } else {
        // Generic extern calls
        rhs << print_extern_call(op);
    }

    // Special-case halide_print, which has IR that returns int, but really return void.
    // The clean thing to do would be to change the definition of halide_print() to return
    // an ignored int, but as halide_print() has many overrides downstream (and in third-party
    // consumers), this is arguably a simpler fix for allowing halide_print() to work in the C++ backend.
    if (op->name == "halide_print") {
        stream << get_indent() << rhs.str() << ";\n";
        // Make an innocuous assignment value for our caller (probably an Evaluate node) to ignore.
        print_assignment(op->type, "0");
    } else {
        print_assignment(op->type, rhs.str());
    }
}

static int loop_level = 0;
void CodeGen_Xtensa::visit(const For *op) {
    loop_level++;
    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    if (op->for_type == ForType::Parallel) {
        stream << get_indent() << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == ForType::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }

    // if (loop_level == 1) {
    //   stream << get_indent() << "int cycles_start, cycles_stop, cyclesAV; (void)cycles_stop; (void)cyclesAV;\n";
    //   stream << get_indent() << "cycles_start = GetCycleCount();\n";
    // }
    // if (loop_level == 2) {
    //   stream << get_indent() << "cycles_start = GetCycleCount();\n";
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

    // if (loop_level == 2) {
    //   stream << get_indent() << "cycles_stop = GetCycleCount();\n";
    //   stream << get_indent() << "cyclesAV = cycles_stop - cycles_start;\n";
    //   stream << get_indent() << "printf(\"" << op->name << ": %d\\n\", cyclesAV);\n";
    // }

    loop_level--;
}

void CodeGen_Xtensa::visit(const Shuffle *op) {
    internal_assert(!op->vectors.empty());
    internal_assert(op->vectors[0].type().is_vector());
    for (size_t i = 1; i < op->vectors.size(); i++) {
        internal_assert(op->vectors[0].type() == op->vectors[i].type());
    }
    internal_assert(op->type.lanes() == (int)op->indices.size());
    const int max_index = (int)(op->vectors[0].type().lanes() * op->vectors.size());
    for (int i : op->indices) {
        internal_assert(i >= -1 && i < max_index);
    }

    std::vector<string> vecs;
    for (Expr v : op->vectors) {
        vecs.push_back(print_expr(v));
    }
    string src = vecs[0];
    if (op->vectors.size() > 1) {
        ostringstream rhs;
        if (vecs.size() == 2) {
            rhs << print_type(op->type) << "::concat(" << with_commas(vecs) << ")";
            src = print_assignment(op->type, rhs.str());
        } else {
            string storage_name = unique_name('_');
            stream << get_indent() << "const " << print_type(op->vectors[0].type()) << " " << storage_name << "[] = { " << with_commas(vecs) << " };\n";
        }
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        rhs << src << "[" << op->indices[0] << "]";
    } else if (op->is_concat()) {
        // Do nothing if it's just concat.
        return;
    } else {
        string indices_name = unique_name('_');
        stream << get_indent() << "const int32_t " << indices_name << "[" << op->indices.size() << "] = { " << with_commas(op->indices) << " };\n";
        rhs << "halide_xtensa_dynamic_shuffle(" << src << ", " << indices_name << ")";
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
        if (!on_stack || is_zero(op->condition)) {
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
        } else {
            stream << "*"
                   << "__attribute__((aligned(64))) "
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

        stream << get_indent();
        string free_function = op->free_function.empty() ? "halide_free" : op->free_function;
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

}  // namespace Internal
}  // namespace Halide

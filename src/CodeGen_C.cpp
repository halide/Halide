#include <iostream>
#include <limits>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "Deinterleave.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Param.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Type.h"
#include "Util.h"
#include "Var.h"
#include "XtensaOptimize.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

extern "C" unsigned char halide_internal_initmod_inlined_c[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntime_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeCuda_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeHexagonHost_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeMetal_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenCL_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenGLCompute_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenGL_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeQurt_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeD3D12Compute_h[];

namespace {

// HALIDE_MUST_USE_RESULT defined here is intended to exactly
// duplicate the definition in HalideRuntime.h (so that either or
// both can be present, in any order).
const char *const kDefineMustUseResult = R"INLINE_CODE(#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif
)INLINE_CODE";

const string headers =
    // "#include <iostream>\n"
    "#include <math.h>\n"
    "#include <float.h>\n"
    "#include <assert.h>\n"
    "#include <limits.h>\n"
    "#include <string.h>\n"
    "#include <stdio.h>\n"
    "#include <stdint.h>\n";

// We now add definitions of things in the runtime which are
// intended to be inlined into every module but are only expressed
// in .ll. The redundancy is regrettable (FIXME).
const string globals = R"INLINE_CODE(
#define constexpr const
#define nullptr NULL

extern "C" {
int64_t halide_current_time_ns(void *ctx);
void halide_profiler_pipeline_end(void *, void *);
}

#ifdef _WIN32
__declspec(dllimport) float __cdecl roundf(float);
__declspec(dllimport) double __cdecl round(double);
#else
inline float asinh_f32(float x) {return asinhf(x);}
inline float acosh_f32(float x) {return acoshf(x);}
inline float atanh_f32(float x) {return atanhf(x);}
inline double asinh_f64(double x) {return asinh(x);}
inline double acosh_f64(double x) {return acosh(x);}
inline double atanh_f64(double x) {return atanh(x);}
#endif
inline float sqrt_f32(float x) {return sqrtf(x);}
inline float sin_f32(float x) {return sinf(x);}
inline float asin_f32(float x) {return asinf(x);}
inline float cos_f32(float x) {return cosf(x);}
inline float acos_f32(float x) {return acosf(x);}
inline float tan_f32(float x) {return tanf(x);}
inline float atan_f32(float x) {return atanf(x);}
inline float atan2_f32(float x, float y) {return atan2f(x, y);}
inline float sinh_f32(float x) {return sinhf(x);}
inline float cosh_f32(float x) {return coshf(x);}
inline float tanh_f32(float x) {return tanhf(x);}
inline float hypot_f32(float x, float y) {return hypotf(x, y);}
inline float exp_f32(float x) {return expf(x);}
inline float log_f32(float x) {return logf(x);}
inline float pow_f32(float x, float y) {return powf(x, y);}
inline float floor_f32(float x) {return floorf(x);}
inline float ceil_f32(float x) {return ceilf(x);}
inline float round_f32(float x) {return roundf(x);}

inline double sqrt_f64(double x) {return sqrt(x);}
inline double sin_f64(double x) {return sin(x);}
inline double asin_f64(double x) {return asin(x);}
inline double cos_f64(double x) {return cos(x);}
inline double acos_f64(double x) {return acos(x);}
inline double tan_f64(double x) {return tan(x);}
inline double atan_f64(double x) {return atan(x);}
inline double atan2_f64(double x, double y) {return atan2(x, y);}
inline double sinh_f64(double x) {return sinh(x);}
inline double cosh_f64(double x) {return cosh(x);}
inline double tanh_f64(double x) {return tanh(x);}
inline double hypot_f64(double x, double y) {return hypot(x, y);}
inline double exp_f64(double x) {return exp(x);}
inline double log_f64(double x) {return log(x);}
inline double pow_f64(double x, double y) {return pow(x, y);}
inline double floor_f64(double x) {return floor(x);}
inline double ceil_f64(double x) {return ceil(x);}
inline double round_f64(double x) {return round(x);}

inline float nan_f32() {return NAN;}
inline float neg_inf_f32() {return -INFINITY;}
inline float inf_f32() {return INFINITY;}
inline bool is_nan_f32(float x) {return isnan(x);}
inline bool is_nan_f64(double x) {return isnan(x);}
inline bool is_inf_f32(float x) {return isinf(x);}
inline bool is_inf_f64(double x) {return isinf(x);}
inline bool is_finite_f32(float x) {return isfinite(x);}
inline bool is_finite_f64(double x) {return isfinite(x);}

template<typename A, typename B>
inline A reinterpret(const B &b) {
    #if __cplusplus >= 201103L
    static_assert(sizeof(A) == sizeof(B), "type size mismatch");
    #endif
    A a;
    memcpy(&a, &b, sizeof(a));
    return a;
}
inline float float_from_bits(uint32_t bits) {
    return reinterpret<float, uint32_t>(bits);
}

template<typename T>
inline int halide_popcount(T a) {
    int bits_set = 0;
    while (a != 0) {
        bits_set += a & 1;
        a >>= 1;
    }
    return bits_set;
}

template<typename T>
inline int halide_count_leading_zeros(T a) {
    int leading_zeros = 0;
    int bit = sizeof(a) * 8 - 1;
    while (bit >= 0 && (a & (((T)1) << bit)) == 0) {
        leading_zeros++;
        bit--;
    }
    return leading_zeros;
}

template<typename T>
inline int halide_count_trailing_zeros(T a) {
    int trailing_zeros = 0;
    constexpr int bits = sizeof(a) * 8;
    int bit = 0;
    while (bit < bits && (a & (((T)1) << bit)) == 0) {
        trailing_zeros++;
        bit++;
    }
    return trailing_zeros;
}

template<typename T>
inline T halide_cpp_max(const T &a, const T &b) {return (a > b) ? a : b;}

template<typename T>
inline T halide_cpp_min(const T &a, const T &b) {return (a < b) ? a : b;}

template<typename T>
inline void halide_unused(const T&) {}

template<typename A, typename B>
const B &return_second(const A &a, const B &b) {
    halide_unused(a);
    return b;
}

namespace {
class HalideFreeHelper {
    typedef void (*FreeFunction)(void *user_context, void *p);
    void * user_context;
    void *p;
    FreeFunction free_function;
public:
    HalideFreeHelper(void *user_context, void *p, FreeFunction free_function)
        : user_context(user_context), p(p), free_function(free_function) {}
    ~HalideFreeHelper() { free(); }
    void free() {
        if (p) {
            // TODO: do all free_functions guarantee to ignore a nullptr?
            free_function(user_context, p);
            p = nullptr;
        }
    }
};
} // namespace
)INLINE_CODE";
}  // namespace

class TypeInfoGatherer : public IRGraphVisitor {
private:
    using IRGraphVisitor::include;
    using IRGraphVisitor::visit;

    void include_type(const Type &t) {
        if (t.is_vector()) {
            if (t.is_bool()) {
                // bool vectors are always emitted as uint8 in the C++ backend
                // TODO: on some architectures, we could do better by choosing
                // a bitwidth that matches the other vectors in use; EliminateBoolVectors
                // could be used for this with a bit of work.
                vector_types_used.insert(UInt(8).with_lanes(t.lanes()));
            } else if (!t.is_handle()) {
                // Vector-handle types can be seen when processing (e.g.)
                // require() statements that are vectorized, but they
                // will all be scalarized away prior to use, so don't emit
                // them.
                vector_types_used.insert(t);
                if (t.is_int()) {
                    // If we are including an int-vector type, also include
                    // the same-width uint-vector type; there are various operations
                    // that can use uint vectors for intermediate results (e.g. lerp(),
                    // but also Mod, which can generate a call to abs() for int types,
                    // which always produces uint results for int inputs in Halide);
                    // it's easier to just err on the side of extra vectors we don't
                    // use since they are just type declarations.
                    vector_types_used.insert(t.with_code(halide_type_uint));
                }
            }
        }
    }

    void include_lerp_types(const Type &t) {
        if (t.is_vector() && t.is_int_or_uint() && (t.bits() >= 8 && t.bits() <= 32)) {
            Type doubled = t.with_bits(t.bits() * 2);
            include_type(doubled);
        }
    }

protected:
    void include(const Expr &e) override {
        include_type(e.type());
        IRGraphVisitor::include(e);
    }

    // GCC's __builtin_shuffle takes an integer vector of
    // the size of its input vector. Make sure this type exists.
    void visit(const Shuffle *op) override {
        vector_types_used.insert(Int(32, op->vectors[0].type().lanes()));
        IRGraphVisitor::visit(op);
    }

    void visit(const For *op) override {
        for_types_used.insert(op->for_type);
        IRGraphVisitor::visit(op);
    }

    void visit(const Ramp *op) override {
        include_type(op->type.with_lanes(op->lanes));
        IRGraphVisitor::visit(op);
    }

    void visit(const Broadcast *op) override {
        include_type(op->type.with_lanes(op->lanes));
        IRGraphVisitor::visit(op);
    }

    void visit(const Cast *op) override {
        include_type(op->type);
        IRGraphVisitor::visit(op);
    }

    void visit(const Call *op) override {
        include_type(op->type);
        if (op->is_intrinsic(Call::lerp)) {
            // lower_lerp() can synthesize wider vector types.
            for (auto &a : op->args) {
                include_lerp_types(a.type());
            }
        }

        IRGraphVisitor::visit(op);
    }

public:
    std::set<ForType> for_types_used;
    std::set<Type> vector_types_used;
};

CodeGen_C::CodeGen_C(ostream &s, Target t, OutputKind output_kind, const std::string &guard)
    : IRPrinter(s), id("$$ BAD ID $$"), target(t), output_kind(output_kind),
      extern_c_open(false), inside_atomic_mutex_node(false), emit_atomic_stores(false) {

    if (is_header()) {
        // If it's a header, emit an include guard.
        stream << "#ifndef HALIDE_" << print_name(guard) << "\n"
               << "#define HALIDE_" << print_name(guard) << "\n"
               << "#include <stdint.h>\n"
               << "\n"
               << "// Forward declarations of the types used in the interface\n"
               << "// to the Halide pipeline.\n"
               << "//\n";
        if (target.has_feature(Target::NoRuntime)) {
            stream << "// For the definitions of these structs, include HalideRuntime.h\n";
        } else {
            stream << "// Definitions for these structs are below.\n";
        }
        stream << "\n"
               << "// Halide's representation of a multi-dimensional array.\n"
               << "// Halide::Runtime::Buffer is a more user-friendly wrapper\n"
               << "// around this. Its declaration is in HalideBuffer.h\n"
               << "struct halide_buffer_t;\n"
               << "\n"
               << "// Metadata describing the arguments to the generated function.\n"
               << "// Used to construct calls to the _argv version of the function.\n"
               << "struct halide_filter_metadata_t;\n"
               << "\n";
        // We just forward declared the following types:
        forward_declared.insert(type_of<halide_buffer_t *>().handle_type);
        forward_declared.insert(type_of<halide_filter_metadata_t *>().handle_type);
    } else if (is_extern_decl()) {
        // Extern decls to be wrapped inside other code (eg python extensions);
        // emit the forward decls with a minimum of noise. Note that we never
        // mess with legacy buffer types in this case.
        stream << "struct halide_buffer_t;\n"
               << "struct halide_filter_metadata_t;\n"
               << "\n";
        forward_declared.insert(type_of<halide_buffer_t *>().handle_type);
        forward_declared.insert(type_of<halide_filter_metadata_t *>().handle_type);
    } else {
        // Include declarations of everything generated C source might want
        stream
            << headers
            << globals
            << halide_internal_runtime_header_HalideRuntime_h << "\n"
            << halide_internal_initmod_inlined_c << "\n";
        add_common_macros(stream);
        stream << "\n";
    }

    stream << kDefineMustUseResult << "\n";

    // Throw in a default (empty) definition of HALIDE_FUNCTION_ATTRS
    // (some hosts may define this to e.g. __attribute__((warn_unused_result)))
    stream << "#ifndef HALIDE_FUNCTION_ATTRS\n";
    stream << "#define HALIDE_FUNCTION_ATTRS\n";
    stream << "#endif\n";
}

CodeGen_C::~CodeGen_C() {
    set_name_mangling_mode(NameMangling::Default);

    if (is_header()) {
        if (!target.has_feature(Target::NoRuntime)) {
            stream << "\n"
                   << "// The generated object file that goes with this header\n"
                   << "// includes a full copy of the Halide runtime so that it\n"
                   << "// can be used standalone. Declarations for the functions\n"
                   << "// in the Halide runtime are below.\n";
            if (target.os == Target::Windows) {
                stream
                    << "//\n"
                    << "// The inclusion of this runtime means that it is not legal\n"
                    << "// to link multiple Halide-generated object files together.\n"
                    << "// This problem is Windows-specific. On other platforms, we\n"
                    << "// use weak linkage.\n";
            } else {
                stream
                    << "//\n"
                    << "// The runtime is defined using weak linkage, so it is legal\n"
                    << "// to link multiple Halide-generated object files together,\n"
                    << "// or to clobber any of these functions with your own\n"
                    << "// definition.\n";
            }
            stream << "//\n"
                   << "// To generate an object file without a full copy of the\n"
                   << "// runtime, use the -no_runtime target flag. To generate a\n"
                   << "// standalone Halide runtime to use with such object files\n"
                   << "// use the -r flag with any Halide generator binary, e.g.:\n"
                   << "// $ ./my_generator -r halide_runtime -o . target=host\n"
                   << "\n"
                   << halide_internal_runtime_header_HalideRuntime_h << "\n";
            if (target.has_feature(Target::CUDA)) {
                stream << halide_internal_runtime_header_HalideRuntimeCuda_h << "\n";
            }
            if (target.has_feature(Target::HVX_128) ||
                target.has_feature(Target::HVX_64)) {
                stream << halide_internal_runtime_header_HalideRuntimeHexagonHost_h << "\n";
            }
            if (target.has_feature(Target::Metal)) {
                stream << halide_internal_runtime_header_HalideRuntimeMetal_h << "\n";
            }
            if (target.has_feature(Target::OpenCL)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenCL_h << "\n";
            }
            if (target.has_feature(Target::OpenGLCompute)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenGLCompute_h << "\n";
            }
            if (target.has_feature(Target::OpenGL)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenGL_h << "\n";
            }
            if (target.has_feature(Target::D3D12Compute)) {
                stream << halide_internal_runtime_header_HalideRuntimeD3D12Compute_h << "\n";
            }
        }
        stream << "#endif\n";
    }
}

void CodeGen_C::add_common_macros(std::ostream &dest) {
    const char *macros = R"INLINE_CODE(
// ll suffix in OpenCL is reserved for 128-bit integers.
#if defined __OPENCL_VERSION__
#define ADD_INT64_T_SUFFIX(x) x##l
#define ADD_UINT64_T_SUFFIX(x) x##ul
// HLSL doesn't have any suffixes.
#elif defined HLSL_VERSION
#define ADD_INT64_T_SUFFIX(x) x
#define ADD_UINT64_T_SUFFIX(x) x
#else
#define ADD_INT64_T_SUFFIX(x) x##ll
#define ADD_UINT64_T_SUFFIX(x) x##ull
#endif
)INLINE_CODE";
    dest << macros;
}

void CodeGen_C::add_vector_typedefs(const std::set<Type> &vector_types) {
    if (!vector_types.empty()) {
        // MSVC has a limit of ~16k for string literals, so split
        // up these declarations accordingly
        const char *cpp_vector_decl = R"INLINE_CODE(
#if !defined(__has_attribute)
    #define __has_attribute(x) 0
#endif

#if !defined(__has_builtin)
    #define __has_builtin(x) 0
#endif

template <typename ElementType_, size_t Lanes_>
class CppVector {
public:
    typedef ElementType_ ElementType;
    static const size_t Lanes = Lanes_;
    typedef CppVector<ElementType, Lanes> Vec;
    typedef CppVector<uint8_t, Lanes> Mask;

    CppVector &operator=(const Vec &src) {
        if (this != &src) {
            for (size_t i = 0; i < Lanes; i++) {
                elements[i] = src[i];
            }
        }
        return *this;
    }

    /* not-explicit */ CppVector(const Vec &src) {
        for (size_t i = 0; i < Lanes; i++) {
            elements[i] = src[i];
        }
    }

    CppVector() {
        for (size_t i = 0; i < Lanes; i++) {
            elements[i] = 0;
        }
    }

    static Vec broadcast(const ElementType &v) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = v;
        }
        return r;
    }

    static Vec ramp(const ElementType &base, const ElementType &stride) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = base + stride * i;
        }
        return r;
    }

    static Vec aligned_load(const void *base, int32_t offset) {
        Vec r(empty);
        memcpy(&r.elements[0], ((const ElementType*)base + offset), sizeof(r.elements));
        return r;
    }

    static Vec load(const void *base, int32_t offset) {
        Vec r(empty);
        memcpy(&r.elements[0], ((const ElementType*)base + offset), sizeof(r.elements));
        return r;
    }

    // gather
    static Vec load(const void *base, const CppVector<int32_t, Lanes> &offset) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = ((const ElementType*)base)[offset[i]];
        }
        return r;
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &this->elements[0], sizeof(this->elements));
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &this->elements[0], sizeof(this->elements));
    }

    // scatter
    void store(void *base, const CppVector<int32_t, Lanes> &offset) const {
        for (size_t i = 0; i < Lanes; i++) {
            ((ElementType*)base)[offset[i]] = elements[i];
        }
    }

    static Vec shuffle(const Vec &a, const int32_t indices[Lanes]) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            if (indices[i] < 0) {
                continue;
            }
            r.elements[i] = a[indices[i]];
        }
        return r;
    }

    template<size_t InputLanes>
    static Vec concat(size_t count, const CppVector<ElementType, InputLanes> vecs[]) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = vecs[i / InputLanes][i % InputLanes];
        }
        return r;
    }

    Vec replace(size_t i, const ElementType &b) const {
        Vec r = *this;
        r.elements[i] = b;
        return r;
    }

    ElementType operator[](size_t i) const {
        return elements[i];
    }

    Vec operator~() const {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = ~elements[i];
        }
        return r;
    }
    Vec operator!() const {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = !r.elements[i];
        }
        return r;
    }

    static Vec count_leading_zeros(const Vec &a) {
        Vec r(empty);

        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = halide_count_leading_zeros(a[i]);
        }

        return r;
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] + b[i];
        }
        return r;
    }
    friend Vec operator-(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] - b[i];
        }
        return r;
    }
    friend Vec operator*(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] * b[i];
        }
        return r;
    }
    friend Vec operator/(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] / b[i];
        }
        return r;
    }
    friend Vec operator%(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] % b[i];
        }
        return r;
    }
    template <typename OtherElementType>
    friend Vec operator<<(const Vec &a, const CppVector<OtherElementType, Lanes> &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] << b[i];
        }
        return r;
    }
    template <typename OtherElementType>
    friend Vec operator>>(const Vec &a, const CppVector<OtherElementType, Lanes> &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] >> b[i];
        }
        return r;
    }
    friend Vec operator&(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] & b[i];
        }
        return r;
    }
    friend Vec operator|(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] | b[i];
        }
        return r;
    }

    friend Vec operator&&(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] && b[i];
        }
        return r;
    }
    friend Vec operator||(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] || b[i];
        }
        return r;
    }

    friend Vec operator+(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] + b;
        }
        return r;
    }
    friend Vec operator-(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] - b;
        }
        return r;
    }
    friend Vec operator*(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] * b;
        }
        return r;
    }
    friend Vec operator/(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] / b;
        }
        return r;
    }
    friend Vec operator%(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] % b;
        }
        return r;
    }
    friend Vec operator>>(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] >> b;
        }
        return r;
    }
    friend Vec operator<<(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] << b;
        }
        return r;
    }
    friend Vec operator&(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] & b;
        }
        return r;
    }
    friend Vec operator|(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] | b;
        }
        return r;
    }
    friend Vec operator&&(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] && b;
        }
        return r;
    }
    friend Vec operator||(const Vec &a, const ElementType &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] || b;
        }
        return r;
    }

    friend Vec operator+(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a + b[i];
        }
        return r;
    }
    friend Vec operator-(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a - b[i];
        }
        return r;
    }
    friend Vec operator*(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a * b[i];
        }
        return r;
    }
    friend Vec operator/(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a / b[i];
        }
        return r;
    }
    friend Vec operator%(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a % b[i];
        }
        return r;
    }
    friend Vec operator>>(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a >> b[i];
        }
        return r;
    }
    friend Vec operator<<(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a << b[i];
        }
        return r;
    }
    friend Vec operator&(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a & b[i];
        }
        return r;
    }
    friend Vec operator|(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a | b[i];
        }
        return r;
    }
    friend Vec operator&&(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a && b[i];
        }
        return r;
    }
    friend Vec operator||(const ElementType &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a || b[i];
        }
        return r;
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] < b[i] ? 0xff : 0x00;
        }
        return r;
    }

    friend Mask operator<=(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] <= b[i] ? 0xff : 0x00;
        }
        return r;
    }

    friend Mask operator>(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] > b[i] ? 0xff : 0x00;
        }
        return r;
    }

    friend Mask operator>=(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] >= b[i] ? 0xff : 0x00;
        }
        return r;
    }

    friend Mask operator==(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] == b[i] ? 0xff : 0x00;
        }
        return r;
    }

    friend Mask operator!=(const Vec &a, const Vec &b) {
        Mask r;
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = a[i] != b[i] ? 0xff : 0x00;
        }
        return r;
    }

    static Vec select(const Mask &cond, const Vec &true_value, const Vec &false_value) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = cond[i] ? true_value[i] : false_value[i];
        }
        return r;
    }

    template <typename OtherVec>
    static Vec convert_from(const OtherVec &src) {
        #if __cplusplus >= 201103L
        static_assert(Vec::Lanes == OtherVec::Lanes, "Lanes mismatch");
        #endif
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = static_cast<typename Vec::ElementType>(src[i]);
        }
        return r;
    }

    static Vec max(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = ::halide_cpp_max(a[i], b[i]);
        }
        return r;
    }

    static Vec min(const Vec &a, const Vec &b) {
        Vec r(empty);
        for (size_t i = 0; i < Lanes; i++) {
            r.elements[i] = ::halide_cpp_min(a[i], b[i]);
        }
        return r;
    }

private:
    template <typename, size_t> friend class CppVector;
    ElementType elements[Lanes];

    // Leave vector uninitialized for cases where we overwrite every entry
    enum Empty { empty };
    CppVector(Empty) {}
};

#if 1 // all types
class uint1x32_t {
  vboolN mask_16_t;
  vboolN_2 mask_32_t[2];

  template <typename, size_t> friend class CppVector;

 public:
  enum Empty { empty };

  inline uint1x32_t(Empty) {}

  enum FromCppVector { from_native_vector };

  inline uint1x32_t(FromCppVector, vboolN m) : mask_16_t(m) {
    *((vboolN*)&mask_32_t[0]) = m;
  }
  inline uint1x32_t(FromCppVector, vboolN_2 m0, vboolN_2 m1) {
    mask_32_t[0] = m0;
    mask_32_t[1] = m1;
    mask_16_t = *((vboolN*)&mask_32_t[0]);
  }
};

template <> class CppVector<int16_t, 32>;
template <> class CppVector<uint16_t, 32>;
template <> class CppVector<int32_t, 32>;
template <> class CppVector<uint32_t, 32>;

inline CppVector<int16_t, 32> convert_to_int16x32_from_uint16x32(const CppVector<uint16_t, 32>& src);
inline CppVector<int16_t, 32> convert_to_int16x32_from_int32x32(const CppVector<int32_t, 32>& src);
inline CppVector<int16_t, 32> convert_to_int16x32_from_uint32x32(const CppVector<uint32_t, 32>& src);
inline CppVector<uint16_t, 32> convert_to_uint16x32_from_int32x32(const CppVector<int32_t, 32>& src);
inline CppVector<uint16_t, 32> convert_to_uint16x32_from_uint32x32(const CppVector<uint32_t, 32>& src);

#if 1
template <>
class CppVector<int16_t, 32> {
  typedef CppVector<int16_t, 32> Vec;
  typedef int16_t ElementType;
  typedef xb_vecNx16 CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;

  template <typename, size_t> friend class CppVector;
public:
    CppVectorType native_vector;

    enum Empty { empty };
    inline CppVector(Empty) {}

    enum FromCppVector { from_native_vector };
    inline CppVector(FromCppVector, const CppVectorType &src) {
        native_vector = src;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v);
    }

    static Vec aligned_load(const void *base, int32_t offset) {
        return Vec(from_native_vector, *((const CppVectorType *)((ElementType*)base + offset)));
    }

    // TODO: could this be improved by taking advantage of native operator support?
    static Vec load(const void *base, int32_t offset) {
        xb_vec2Nx8 nv8;
        xb_vec2Nx8* ptr = (xb_vec2Nx8*)((const ElementType*)base + offset);
        IVP_L2U2NX8_XP(nv8, ptr, 0);
        return Vec(from_native_vector, IVP_MOVNX16_FROM2NX8(nv8));
    }

    template <typename OtherVec>
    static Vec load(const void *base, const OtherVec& offset) {
        ElementType tmp[Lanes];
        int offsets[Lanes];
        offset.store(&offsets[0], 0);
        for (int i = 0; i < Lanes; i++) {
            tmp[i] = ((const ElementType*)base)[offsets[i]];
        }

        return Vec(from_native_vector, *((CppVectorType*)tmp));
    }

    void aligned_store(void *base, int32_t offset) const {
        *((CppVectorType *)((ElementType*)base + offset)) = native_vector;
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector, sizeof(ElementType) * Lanes);
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector + b.native_vector);
    }

    friend Vec operator-(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector - b.native_vector);
    }

    friend Vec operator*(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_MULNX16PACKL(a.native_vector, b.native_vector));
    }

    friend Vec operator>>(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SRANX16(a.native_vector, b.native_vector));
    }

    friend Vec operator<<(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SLANX16(a.native_vector, b.native_vector));
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return Mask(uint1x32_t::from_native_vector, a.native_vector < b.native_vector);
    }

    ElementType operator[](size_t i) const {
        ElementType tmp[Lanes];
        memcpy(&tmp[0], &native_vector, sizeof(ElementType) * Lanes);
        return tmp[i];
    }

    static Vec select(const Mask &cond, const Vec &true_value, const Vec &false_value) {
        return Vec(from_native_vector, IVP_MOVNX16T(true_value.native_vector, false_value.native_vector, cond.mask_16_t));
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<uint16_t, 32>& src) {
      return convert_to_int16x32_from_uint16x32(src);
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<int32_t, 32>& src) {
      return convert_to_int16x32_from_int32x32(src);
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<uint32_t, 32>& src) {
        return convert_to_int16x32_from_uint32x32(src);
    }

    // TODO: this should be improved by taking advantage of native operator support.
    static Vec max(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_MAXNX16(a.native_vector, b.native_vector));
    }

    // TODO: this should be improved by taking advantage of native operator support.
    static Vec min(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_MINNX16(a.native_vector, b.native_vector));
    }

    static Vec count_leading_zeros(const Vec &a) {
        return Vec(from_native_vector, IVP_NSANX16(a.native_vector));
    }
};
#endif

#if 1
template <>
class CppVector<uint16_t, 32> {
  typedef CppVector<uint16_t, 32> Vec;
  typedef uint16_t ElementType;
  typedef xb_vecNx16U CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;


  template <typename, size_t> friend class CppVector;
  friend CppVector<int16_t, 32> convert_to_int16x32_from_uint16x32(const CppVector<uint16_t, 32>& src);
public:
    CppVectorType native_vector;

    enum Empty { empty };
    inline CppVector(Empty) {}

    enum FromCppVector { from_native_vector };
    inline CppVector(FromCppVector, const CppVectorType &src) {
        native_vector = src;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v);
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector + b.native_vector);
    }

    friend Vec operator-(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector - b.native_vector);
    }

    friend Vec operator>>(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SRANX16(a.native_vector, b.native_vector));
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return Mask(uint1x32_t::from_native_vector, a.native_vector < b.native_vector);
    }


    template <typename OtherVec>
    static Vec convert_from(const CppVector<int16_t, 32>& src) {
        return Vec(from_native_vector, src.native_vector);
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<int32_t, 32>& src) {
        return convert_to_uint16x32_from_int32x32(src);
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<uint32_t, 32>& src) {
        return convert_to_uint16x32_from_uint32x32(src);
    }

    static Vec count_leading_zeros(const Vec &a) {
        return Vec(from_native_vector, IVP_NSAUNX16(a.native_vector));
    }
};
#endif

#if 1
template <>
class CppVector<int32_t, 32> {
  typedef CppVector<int32_t, 32> Vec;
  typedef int32_t ElementType;
  typedef xb_vecN_2x32v CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;

  template <typename, size_t> friend class CppVector;
  friend CppVector<int16_t, 32> convert_to_int16x32_from_int32x32(const CppVector<int32_t, 32>& src);
  friend CppVector<uint16_t, 32> convert_to_uint16x32_from_int32x32(const CppVector<int32_t, 32>& src);
public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline CppVector(Empty) {}

    enum FromCppVector { from_native_vector };
    inline CppVector(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v, v);
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

    // TODO: could this be improved by taking advantage of native operator support?
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

    friend Vec operator>>(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] >> b.native_vector[0], a.native_vector[1] >> b.native_vector[1]);
    }

    friend Vec operator<<(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] << b.native_vector[0], a.native_vector[1] << b.native_vector[1]);
    }

    ElementType operator[](size_t i) const {
        ElementType tmp[Lanes];
        memcpy(&tmp[0], &native_vector[0], sizeof(ElementType) * Lanes);
        return tmp[i];
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return Mask(uint1x32_t::from_native_vector,
                    a.native_vector[0] < b.native_vector[0],
                    a.native_vector[1] < b.native_vector[1]);
    }

    static Vec select(const Mask &cond, const Vec &true_value, const Vec &false_value) {
        return Vec(from_native_vector,
                    IVP_MOVN_2X32T(true_value.native_vector[0], false_value.native_vector[0], cond.mask_32_t[0]),
                    IVP_MOVN_2X32T(true_value.native_vector[1], false_value.native_vector[1], cond.mask_32_t[1]));
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<int16_t, 32>& src) {
        xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, src.native_vector);
        return Vec(from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<int64_t, 32>& src) {
        Vec r(empty);

        ElementType tmp[Lanes];
        for (int i = 0; i < Lanes; i++) {
            tmp[i] = static_cast<typename Vec::ElementType>(src[i]);
        }
        memcpy(&r.native_vector, &tmp[0], sizeof(ElementType) * Lanes);

        return r;
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
#endif
#if 1
template <>
class CppVector<uint32_t, 32> {
  typedef CppVector<uint32_t, 32> Vec;
  typedef uint32_t ElementType;
  typedef xb_vecN_2x32Uv CppVectorType;
  static const int Lanes = 32;
  typedef uint1x32_t Mask;

  CppVectorType native_vector[2];

  template <typename, size_t> friend class CppVector;
  friend CppVector<int16_t, 32> convert_to_int16x32_from_uint32x32(const CppVector<uint32_t, 32>& src);
  friend CppVector<uint16_t, 32> convert_to_uint16x32_from_uint32x32(const CppVector<uint32_t, 32>& src);
public:
    enum Empty { empty };
    inline CppVector(Empty) {}

    enum FromCppVector { from_native_vector };
    inline CppVector(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    static Vec broadcast(const ElementType &v) {
        return Vec(from_native_vector, v, v);
    }

    friend Vec operator+(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, a.native_vector[0] + b.native_vector[0], a.native_vector[1] + b.native_vector[1]);
    }

    friend Vec operator*(const Vec &a, const Vec &b) {
        return Vec(from_native_vector,
                    IVP_PACKLN_2X64W(IVP_MULN_2X32(a.native_vector[0], b.native_vector[0])),
                    IVP_PACKLN_2X64W(IVP_MULN_2X32(a.native_vector[1], b.native_vector[1])));
    }

    friend Vec operator>>(const Vec &a, const Vec &b) {
        return Vec(from_native_vector, IVP_SLAN_2X32(a.native_vector[0], b.native_vector[0]),
                                       IVP_SLAN_2X32(a.native_vector[1], b.native_vector[1]));
    }

    friend Mask operator<(const Vec &a, const Vec &b) {
        return Mask(uint1x32_t::from_native_vector,
                    a.native_vector[0] < b.native_vector[0],
                    a.native_vector[1] < b.native_vector[1]);
    }

    template <typename OtherVec>
    static Vec convert_from(const CppVector<uint16_t, 32>& src) {
        xb_vec2Nx24 wide = IVP_CVT24U2NX16(0, src.native_vector);
        return Vec(from_native_vector, IVP_CVT32S2NX24LL(wide), IVP_CVT32S2NX24LH(wide));
    }

    static Vec count_leading_zeros(const Vec &a) {
        return Vec(from_native_vector, IVP_NSAUN_2X32(a.native_vector[0]), IVP_NSAUN_2X32(a.native_vector[1]));
    }
};
#endif
#if 1
template <>
class CppVector<int16_t, 64> {
  typedef CppVector<int16_t, 64> Vec;
  typedef int16_t ElementType;
  typedef xb_vecNx16 CppVectorType;
  static const int Lanes = 64;

  template <typename, size_t> friend class CppVector;
public:

    CppVectorType native_vector[2];

    enum Empty { empty };
    inline CppVector(Empty) {}

    enum FromCppVector { from_native_vector };
    inline CppVector(FromCppVector, const CppVectorType &src1, const CppVectorType &src2) {
        native_vector[0] = src1;
        native_vector[1] = src2;
    }

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }
};
#endif

inline CppVector<int16_t, 32> convert_to_int16x32_from_uint16x32(const CppVector<uint16_t, 32>& src) {
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, src.native_vector);
}

inline CppVector<int16_t, 32> convert_to_int16x32_from_int32x32(const CppVector<int32_t, 32>& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_PACKLNX48(wide));
}

inline CppVector<int16_t, 32> convert_to_int16x32_from_uint32x32(const CppVector<uint32_t, 32>& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_PACKLNX48(wide));
}

inline CppVector<uint16_t, 32> convert_to_uint16x32_from_int32x32(const CppVector<int32_t, 32>& src) {
  xb_vecNx48 wide = IVP_CVT48SNX32(src.native_vector[1], src.native_vector[0]);
  return CppVector<uint16_t, 32>(CppVector<uint16_t, 32>::from_native_vector, IVP_PACKLNX48(wide));
}

inline CppVector<uint16_t, 32> convert_to_uint16x32_from_uint32x32(const CppVector<uint32_t, 32>& src) {
  xb_vecNx48 wide = IVP_CVT48UNX32(src.native_vector[1], src.native_vector[0]);
  return CppVector<uint16_t, 32>(CppVector<uint16_t, 32>::from_native_vector, IVP_PACKLNX48(wide));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 32> halide_xtensa_sat_add_i16(const CppVector<int16_t, 32>& a,
                                                                      const CppVector<int16_t, 32>& b) {
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_ADDSNX16(a.native_vector, b.native_vector));
}

HALIDE_ALWAYS_INLINE CppVector<int32_t, 32> halide_xtensa_sat_add_i32(const CppVector<int32_t, 32>& a,
                                                                      const CppVector<int32_t, 32>& b) {
  // I am not 100% about it.
  xb_vecN_2x32v zero = 0;
  xb_vecN_2x32v one = 1;
  xb_vecN_2x64w l0 = a.native_vector[0] * one;
  IVP_MULAN_2X32(l0, b.native_vector[0], one);
  xb_vecN_2x64w l1 = a.native_vector[1] * one;
  IVP_MULAN_2X32(l1, b.native_vector[1], one);
  return CppVector<int32_t, 32>(CppVector<int32_t, 32>::from_native_vector,
                                IVP_PACKVN_2X64W(l0, zero), IVP_PACKVN_2X64W(l1, zero));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 32> halide_xtensa_sat_sub_i16(const CppVector<int16_t, 32>& a,
                                                                      const CppVector<int16_t, 32>& b) {
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_SUBSNX16(a.native_vector, b.native_vector));
}

HALIDE_ALWAYS_INLINE CppVector<int32_t, 32> halide_xtensa_widen_add_i32(const CppVector<int32_t, 32>& a,
                                                                        const CppVector<int16_t, 32>& b) {
  xb_vec2Nx24 wide = IVP_CVT24S2NX16(0, b.native_vector);
  return CppVector<int32_t, 32>(CppVector<int32_t, 32>::from_native_vector,
                                  IVP_CVT32S2NX24LL(wide) + a.native_vector[0],
                                  IVP_CVT32S2NX24LH(wide) + a.native_vector[1]);
}

HALIDE_ALWAYS_INLINE CppVector<int32_t, 32> halide_xtensa_widen_mul_i32(const CppVector<int16_t, 32>& a,
                                                                        int b) {
  xb_vecNx48 r = a.native_vector * xb_vecNx16(b);
  return CppVector<int32_t, 32>(CppVector<int32_t, 32>::from_native_vector,
                                IVP_CVT32SNX48L(r),
                                IVP_CVT32SNX48H(r));
}

HALIDE_ALWAYS_INLINE CppVector<int32_t, 32> halide_xtensa_widen_mul_i32(const CppVector<int16_t, 32>& a,
                                                                        const CppVector<int16_t, 32>& b) {
  xb_vecNx48 r = a.native_vector * b.native_vector;
  return CppVector<int32_t, 32>(CppVector<int32_t, 32>::from_native_vector,
                                IVP_CVT32SNX48L(r),
                                IVP_CVT32SNX48H(r));
}

HALIDE_ALWAYS_INLINE CppVector<uint32_t, 32> halide_xtensa_widen_mul_u32(const CppVector<uint16_t, 32>& a,
                                                                         const CppVector<uint16_t, 32>& b) {
  xb_vecNx48 r = a.native_vector * b.native_vector;
  return CppVector<uint32_t, 32>(CppVector<uint32_t, 32>::from_native_vector,
                                IVP_CVT32UNX48L(r),
                                IVP_CVT32UNX48H(r));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 32> halide_xtensa_avg_round_i16(const CppVector<int16_t, 32>& a,
                                                                        const CppVector<int16_t, 32>& b) {
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_AVGRNX16(a.native_vector, b.native_vector));
}

HALIDE_ALWAYS_INLINE CppVector<uint16_t, 32> halide_xtensa_avg_round_u16(const CppVector<uint16_t, 32>& a,
                                                                          const CppVector<uint16_t, 32>& b) {
  return CppVector<uint16_t, 32>(CppVector<uint16_t, 32>::from_native_vector, IVP_AVGRUNX16(a.native_vector, b.native_vector));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 32> halide_xtensa_narrow_with_shift_i16(const CppVector<int32_t, 32>& a,
                                                                            int shift) {
  xb_vecNx48 wide = IVP_CVT48SNX32(a.native_vector[1], a.native_vector[0]);
  return CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector, IVP_PACKVRNRNX48(wide, shift));
}

HALIDE_ALWAYS_INLINE CppVector<uint16_t, 32> halide_xtensa_absd_i16(const CppVector<int16_t, 32>& a,
                                                                    const CppVector<int16_t, 32>& b) {
  return CppVector<uint16_t, 32>(CppVector<uint16_t, 32>::from_native_vector, IVP_ABSSUBNX16(a.native_vector, b.native_vector));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 32> halide_xtensa_clamped_dense_load_i16(
          const void *base, int32_t ramp_base, int32_t upper_limit, int32_t lower_limit, int32_t offset) {
  // This is a bit flawed, as it assumes that vector starting at ramp_base
  // interesects with [lower_limit, upper_limit] range.
  xb_vecNx16 mask = IVP_MINNX16(
                        IVP_MAXNX16(IVP_SEQNX16(), xb_vecNx16(lower_limit - ramp_base)),
                        xb_vecNx16(upper_limit - ramp_base));
  CppVector<int16_t, 32> unclamped_vector = CppVector<int16_t, 32>::load(base, ramp_base + offset);
  return  CppVector<int16_t, 32>(CppVector<int16_t, 32>::from_native_vector,
                            IVP_SHFLNX16(unclamped_vector.native_vector, mask));
}

HALIDE_ALWAYS_INLINE CppVector<int16_t, 64> halide_xtensa_interleave_i16(
                                                    const CppVector<int16_t, 32>& a,
                                                    const CppVector<int16_t, 32>& b) {
  const int IVP_SELI_16B_INTERLEAVE_1_LO = 32;
  const int IVP_SELI_16B_INTERLEAVE_1_HI = 33;

  return CppVector<int16_t, 64>(CppVector<int16_t, 64>::from_native_vector,
                                IVP_SELNX16I(b.native_vector, a.native_vector, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b.native_vector, a.native_vector, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
}


#else // all types

typedef CppVector<uint8_t, 32> uint1x32_t;
#endif // all types

)INLINE_CODE";

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

typedef xb_vecNx16 int16x32_t;
typedef xb_vecNx16U uint16x32_t;
typedef xb_vecN_2x32v int32x16_t;
typedef xb_vecN_2x32Uv uint32x16_t;
typedef xb_vecNx48 int48x32_t;
typedef vboolN uint1x32_t;

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

    void aligned_store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }

    void store(void *base, int32_t offset) const {
        memcpy(((ElementType*)base + offset), &native_vector[0], sizeof(ElementType) * Lanes);
    }
};

HALIDE_ALWAYS_INLINE HALIDE_MAYBE_UNUSED int16x32_t int16x32_t_aligned_load(const void *base, int32_t offset) {
    return *((const int16x32_t *)((int16_t*)base + offset));
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

HALIDE_ALWAYS_INLINE void store(const uint16x32_t& a, void *base, int32_t offset) {
    memcpy(((uint16_t*)base + offset), &a, sizeof(uint16_t) * 32);
}

HALIDE_ALWAYS_INLINE void aligned_store(const int16x64_t& a, void *base, int32_t offset) {
   a.aligned_store(base, offset);
   //xb_vecNx16* ptr = (int16x32_t *)((int16_t*)base + offset);
   //ptr[0] = a.native_vector[0];
   //ptr[1] = a.native_vector[1];
}

HALIDE_ALWAYS_INLINE void store(const int16x64_t& a, void *base, int32_t offset) {
  a.store(base, offset);
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

HALIDE_ALWAYS_INLINE int16x32_t halide_xtensa_clamped_dense_load_i16(
          const void *base, int32_t ramp_base, int32_t upper_limit, int32_t lower_limit, int32_t offset) {
  // This is a bit flawed, as it assumes that vector starting at ramp_base
  // interesects with [lower_limit, upper_limit] range.
  xb_vecNx16 mask = IVP_MINNX16(
                        IVP_MAXNX16(IVP_SEQNX16(), xb_vecNx16(lower_limit - ramp_base)),
                        xb_vecNx16(upper_limit - ramp_base));
  int16x32_t unclamped_vector = int16x32_t_load(base, ramp_base + offset);
  return IVP_SHFLNX16(unclamped_vector, mask);
}

HALIDE_ALWAYS_INLINE int16x64_t halide_xtensa_interleave_i16(const int16x32_t& a, const int16x32_t& b) {
  return int16x64_t(int16x64_t::from_native_vector,
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_LO),
                                IVP_SELNX16I(b, a, IVP_SELI_16B_INTERLEAVE_1_HI)
                                );
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

HALIDE_ALWAYS_INLINE int32x16_t halide_xtensa_slice_to_native(const int32x32_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
}

HALIDE_ALWAYS_INLINE int32x32_t halide_xtensa_concat_from_native(const int32x16_t& a, const int32x16_t& b) {
    return int32x32_t(int32x32_t::from_native_vector, a, b);
}

HALIDE_ALWAYS_INLINE uint32x16_t halide_xtensa_slice_to_native(const uint32x32_t& src, int index, int native_lanes, int total_lanes) {
  return src.native_vector[index];
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

inline uint32x16_t halide_xtensa_convert_i48_low_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32UNX48L(src);
}

inline uint32x16_t halide_xtensa_convert_i48_high_u32(const int48x32_t& src, int native_lanes, int total_lines) {
    return IVP_CVT32UNX48H(src);
}

)INLINE_CODE";
        stream << std::flush;
        stream << native_typedef_decl;
        stream << std::flush;
        (void)cpp_vector_decl;
        //         // Vodoo fix: on at least one config (our arm32 buildbot running gcc 5.4),
        //         // emitting this long text string was regularly garbled in a predictable pattern;
        //         // flushing the stream before or after heals it. Since C++ codegen is rarely
        //         // on a compilation critical path, we'll just band-aid it in this way.
        //         stream << std::flush;
        //         stream << cpp_vector_decl << native_vector_decl << vector_selection_decl;
        //         stream << std::flush;

        //         for (const auto &t : vector_types) {
        //             string name = type_to_c_type(t, false, false);
        //             string scalar_name = type_to_c_type(t.element_of(), false, false);
        //             stream << "#if halide_cpp_use_native_vector(" << scalar_name << ", " << t.lanes() << ")\n";
        //             stream << "typedef NativeVector<" << scalar_name << ", " << t.lanes() << "> " << name << ";\n";
        //             // Useful for debugging which Vector implementation is being selected
        //             // stream << "#pragma message \"using NativeVector for " << t << "\"\n";
        //             stream << "#else\n";
        //             stream << "typedef CppVector<" << scalar_name << ", " << t.lanes() << "> " << name << ";\n";
        //             // Useful for debugging which Vector implementation is being selected
        //             // stream << "#pragma message \"using CppVector for " << t << "\"\n";
        //             stream << "#endif\n";
        //         }
    }
}

void CodeGen_C::set_name_mangling_mode(NameMangling mode) {
    if (extern_c_open && mode != NameMangling::C) {
        stream << "\n#ifdef __cplusplus\n";
        stream << "}  // extern \"C\"\n";
        stream << "#endif\n\n";
        extern_c_open = false;
    } else if (!extern_c_open && mode == NameMangling::C) {
        stream << "\n#ifdef __cplusplus\n";
        stream << "extern \"C\" {\n";
        stream << "#endif\n\n";
        extern_c_open = true;
    }
}

string CodeGen_C::print_type(Type type, AppendSpaceIfNeeded space_option) {
    return type_to_c_type(type, space_option == AppendSpace);
}

string CodeGen_C::print_reinterpret(Type type, const Expr &e) {
    ostringstream oss;
    if (type.is_handle() || e.type().is_handle()) {
        // Use a c-style cast if either src or dest is a handle --
        // note that although Halide declares a "Handle" to always be 64 bits,
        // the source "handle" might actually be a 32-bit pointer (from
        // a function parameter), so calling reinterpret<> (which just memcpy's)
        // would be garbage-producing.
        oss << "(" << print_type(type) << ")";
    } else {
        oss << "reinterpret<" << print_type(type) << ">";
    }
    oss << "(" << print_expr(e) << ")";
    return oss.str();
}

string CodeGen_C::print_name(const string &name) {
    return c_print_name(name);
}

namespace {
class ExternCallPrototypes : public IRGraphVisitor {
    struct NamespaceOrCall {
        const Call *call;  // nullptr if this is a subnamespace
        std::map<string, NamespaceOrCall> names;
        NamespaceOrCall(const Call *call = nullptr)
            : call(call) {
        }
    };
    std::map<string, NamespaceOrCall> c_plus_plus_externs;
    std::map<string, const Call *> c_externs;
    std::set<std::string> processed;
    std::set<std::string> internal_linkage;
    std::set<std::string> destructors;

    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);

        if (!processed.count(op->name)) {
            if ((op->call_type == Call::Extern || op->call_type == Call::PureExtern) && op->name.find("halide_xtensa_") != 0) {
                c_externs.insert({op->name, op});
            } else if (op->call_type == Call::ExternCPlusPlus) {
                std::vector<std::string> namespaces;
                std::string name = extract_namespaces(op->name, namespaces);
                std::map<string, NamespaceOrCall> *namespace_map = &c_plus_plus_externs;
                for (const auto &ns : namespaces) {
                    auto insertion = namespace_map->insert({ns, NamespaceOrCall()});
                    namespace_map = &insertion.first->second.names;
                }
                namespace_map->insert({name, NamespaceOrCall(op)});
            }
            processed.insert(op->name);
        }

        if (op->is_intrinsic(Call::register_destructor)) {
            internal_assert(op->args.size() == 2);
            const StringImm *fn = op->args[0].as<StringImm>();
            internal_assert(fn);
            destructors.insert(fn->value);
        }
    }

    void visit(const Allocate *op) override {
        IRGraphVisitor::visit(op);
        if (!op->free_function.empty()) {
            destructors.insert(op->free_function);
        }
    }

    void emit_function_decl(ostream &stream, const Call *op, const std::string &name) const {
        // op->name (rather than the name arg) since we need the fully-qualified C++ name
        if (internal_linkage.count(op->name)) {
            stream << "static ";
        }
        stream << type_to_c_type(op->type, /* append_space */ true) << name << "(";
        if (function_takes_user_context(name)) {
            stream << "void *";
            if (!op->args.empty()) {
                stream << ", ";
            }
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) {
                stream << ", ";
            }
            if (op->args[i].as<StringImm>()) {
                stream << "const char *";
            } else {
                stream << type_to_c_type(op->args[i].type(), true);
            }
        }
        stream << ");\n";
    }

    void emit_namespace_or_call(ostream &stream, const NamespaceOrCall &ns_or_call, const std::string &name) const {
        if (ns_or_call.call == nullptr) {
            stream << "namespace " << name << " {\n";
            for (const auto &ns_or_call_inner : ns_or_call.names) {
                emit_namespace_or_call(stream, ns_or_call_inner.second, ns_or_call_inner.first);
            }
            stream << "} // namespace " << name << "\n";
        } else {
            emit_function_decl(stream, ns_or_call.call, name);
        }
    }

public:
    ExternCallPrototypes() {
        // Make sure we don't catch calls that are already in the global declarations
        const char *strs[] = {globals.c_str(),
                              (const char *)halide_internal_runtime_header_HalideRuntime_h,
                              (const char *)halide_internal_initmod_inlined_c};
        for (const char *str : strs) {
            size_t j = 0;
            for (size_t i = 0; str[i]; i++) {
                char c = str[i];
                if (c == '(' && i > j + 1) {
                    // Could be the end of a function_name.
                    string name(str + j + 1, i - j - 1);
                    processed.insert(name);
                }

                if (('A' <= c && c <= 'Z') ||
                    ('a' <= c && c <= 'z') ||
                    c == '_' ||
                    ('0' <= c && c <= '9')) {
                    // Could be part of a function name.
                } else {
                    j = i;
                }
            }
        }
    }

    void set_internal_linkage(const std::string &name) {
        internal_linkage.insert(name);
    }

    bool has_c_declarations() const {
        return !c_externs.empty();
    }

    bool has_c_plus_plus_declarations() const {
        return !c_plus_plus_externs.empty();
    }

    void emit_c_declarations(ostream &stream) const {
        for (const auto &call : c_externs) {
            emit_function_decl(stream, call.second, call.first);
        }
        for (const auto &d : destructors) {
            stream << "void " << d << "(void *, void *);\n";
        }
        stream << "\n";
    }

    void emit_c_plus_plus_declarations(ostream &stream) const {
        for (const auto &ns_or_call : c_plus_plus_externs) {
            emit_namespace_or_call(stream, ns_or_call.second, ns_or_call.first);
        }
        stream << "\n";
    }
};
}  // namespace

void CodeGen_C::forward_declare_type_if_needed(const Type &t) {
    if (!t.handle_type ||
        forward_declared.count(t.handle_type) ||
        t.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        return;
    }
    for (auto &ns : t.handle_type->namespaces) {
        stream << "namespace " << ns << " { ";
    }
    switch (t.handle_type->inner_name.cpp_type_type) {
    case halide_cplusplus_type_name::Simple:
        // nothing
        break;
    case halide_cplusplus_type_name::Struct:
        stream << "struct " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Class:
        stream << "class " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Union:
        stream << "union " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Enum:
        internal_error << "Passing pointers to enums is unsupported\n";
        break;
    }
    for (auto &ns : t.handle_type->namespaces) {
        (void)ns;
        stream << " }";
    }
    stream << "\n";
    forward_declared.insert(t.handle_type);
}

void CodeGen_C::compile(const Module &input) {
    TypeInfoGatherer type_info;
    for (const auto &f : input.functions()) {
        if (f.body.defined()) {
            f.body.accept(&type_info);
        }
    }
    uses_gpu_for_loops = (type_info.for_types_used.count(ForType::GPUBlock) ||
                          type_info.for_types_used.count(ForType::GPUThread) ||
                          type_info.for_types_used.count(ForType::GPULane));

    // Forward-declare all the types we need; this needs to happen before
    // we emit function prototypes, since those may need the types.
    stream << "\n";
    for (const auto &f : input.functions()) {
        for (auto &arg : f.args) {
            forward_declare_type_if_needed(arg.type);
        }
    }
    stream << "\n";

    if (!is_header_or_extern_decl()) {
        // Emit any external-code blobs that are C++.
        for (const ExternalCode &code_blob : input.external_code()) {
            if (code_blob.is_c_plus_plus_source()) {
                stream << "\n";
                stream << "// Begin External Code: " << code_blob.name() << "\n";
                stream.write((const char *)code_blob.contents().data(), code_blob.contents().size());
                stream << "\n";
                stream << "// End External Code: " << code_blob.name() << "\n";
                stream << "\n";
            }
        }

        add_vector_typedefs(type_info.vector_types_used);

        // Emit prototypes for all external and internal-only functions.
        // Gather them up and do them all up front, to reduce duplicates,
        // and to make it simpler to get internal-linkage functions correct.
        ExternCallPrototypes e;
        for (const auto &f : input.functions()) {
            f.body.accept(&e);
            if (f.linkage == LinkageType::Internal) {
                // We can't tell at the call site if a LoweredFunc is intended to be internal
                // or not, so mark them explicitly.
                e.set_internal_linkage(f.name);
            }
        }

        if (e.has_c_plus_plus_declarations()) {
            set_name_mangling_mode(NameMangling::CPlusPlus);
            e.emit_c_plus_plus_declarations(stream);
        }

        if (e.has_c_declarations()) {
            set_name_mangling_mode(NameMangling::C);
            e.emit_c_declarations(stream);
        }
    }

    for (const auto &b : input.buffers()) {
        compile(b);
    }
    for (const auto &f : input.functions()) {
        compile(f);
    }
}

void CodeGen_C::compile(const LoweredFunc &f) {
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

            // Emit the body
            Stmt body = f.body;
            body = match_xtensa_patterns(body);
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

void CodeGen_C::compile(const Buffer<> &buffer) {
    // Don't define buffers in headers or extern decls.
    if (is_header_or_extern_decl()) {
        return;
    }

    string name = print_name(buffer.name());
    halide_buffer_t b = *(buffer.raw_buffer());

    user_assert(b.host) << "Can't embed image: " << buffer.name() << " because it has a null host pointer\n";
    user_assert(!b.device_dirty()) << "Can't embed image: " << buffer.name() << "because it has a dirty device pointer\n";

    // Figure out the offset of the last pixel.
    size_t num_elems = 1;
    for (int d = 0; d < b.dimensions; d++) {
        num_elems += b.dim[d].stride * (b.dim[d].extent - 1);
    }

    // For now, we assume buffers that aren't scalar are constant,
    // while scalars can be mutated. This accommodates all our existing
    // use cases, which is that all buffers are constant, except those
    // used to store stateful module information in offloading runtimes.
    bool is_constant = buffer.dimensions() != 0;

    // Emit the data
    stream << "static " << (is_constant ? "const" : "") << " uint8_t " << name << "_data[] HALIDE_ATTRIBUTE_ALIGN(32) = {\n";
    stream << get_indent();
    for (size_t i = 0; i < num_elems * b.type.bytes(); i++) {
        if (i > 0) {
            stream << ",";
            if (i % 16 == 0) {
                stream << "\n";
                stream << get_indent();
            } else {
                stream << " ";
            }
        }
        stream << (int)(b.host[i]);
    }
    stream << "\n};\n";

    // Emit the shape (constant even for scalar buffers)
    stream << "static const halide_dimension_t " << name << "_buffer_shape[] = {";
    for (int i = 0; i < buffer.dimensions(); i++) {
        stream << "halide_dimension_t(" << buffer.dim(i).min()
               << ", " << buffer.dim(i).extent()
               << ", " << buffer.dim(i).stride() << ")";
        if (i < buffer.dimensions() - 1) {
            stream << ", ";
        }
    }
    stream << "};\n";

    Type t = buffer.type();

    // Emit the buffer struct. Note that although our shape and (usually) our host
    // data is const, the buffer itself isn't: embedded buffers in one pipeline
    // can be passed to another pipeline (e.g. for an extern stage), in which
    // case the buffer objects need to be non-const, because the constness
    // (from the POV of the extern stage) is a runtime property.
    stream << "static halide_buffer_t " << name << "_buffer_ = {"
           << "0, "                                              // device
           << "nullptr, "                                        // device_interface
           << "const_cast<uint8_t*>(&" << name << "_data[0]), "  // host
           << "0, "                                              // flags
           << "halide_type_t((halide_type_code_t)(" << (int)t.code() << "), " << t.bits() << ", " << t.lanes() << "), "
           << buffer.dimensions() << ", "
           << "const_cast<halide_dimension_t*>(" << name << "_buffer_shape)};\n";

    // Make a global pointer to it.
    stream << "static halide_buffer_t * const " << name << "_buffer = &" << name << "_buffer_;\n";
}

string CodeGen_C::print_expr(const Expr &e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

string CodeGen_C::print_cast_expr(const Type &t, const Expr &e) {
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

void CodeGen_C::print_stmt(const Stmt &s) {
    s.accept(this);
}

string CodeGen_C::print_assignment(Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        stream << get_indent() << print_type(t, AppendSpace) << (output_kind == CPlusPlusImplementation ? "const " : "") << id << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_C::open_scope() {
    cache.clear();
    stream << get_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_C::close_scope(const std::string &comment) {
    cache.clear();
    indent--;
    stream << get_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

bool CodeGen_C::is_native_vector_type(Type t) {
    if (t.is_int_or_uint() && (t.lanes() == 32) && (t.bits() == 16)) {
        return true;
    }

    if (t.is_int_or_uint() && (t.lanes() == 16) && (t.bits() == 32)) {
        return true;
    }

    return false;
}

void CodeGen_C::visit(const Variable *op) {
    id = print_name(op->name);
}

void CodeGen_C::visit(const Cast *op) {
    id = print_cast_expr(op->type, op->value);
}

void CodeGen_C::visit_binop(Type t, const Expr &a, const Expr &b, const char *op) {
    string sa = print_expr(a);
    string sb = print_expr(b);
    print_assignment(t, sa + " " + op + " " + sb);
}

void CodeGen_C::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "+");
}

void CodeGen_C::visit(const Sub *op) {
    visit_binop(op->type, op->a, op->b, "-");
}

void CodeGen_C::visit(const Mul *op) {
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

void CodeGen_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        if (op->type.is_uint() && (op->type.lanes() == 32) && (op->type.bits() == 16)) {
            string sa = print_expr(op->a);
            print_assignment(op->type, "uint16x32_t_shift_right(" + sa + ", " + std::to_string(bits) + ")");
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

void CodeGen_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        visit_binop(op->type, op->a, make_const(op->a.type(), (1 << bits) - 1), "&");
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else if (op->type.is_float()) {
        string arg0 = print_expr(op->a);
        string arg1 = print_expr(op->b);
        ostringstream rhs;
        rhs << "fmod(" << arg0 << ", " << arg1 << ")";
        print_assignment(op->type, rhs.str());
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_C::visit(const Max *op) {
    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
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

void CodeGen_C::visit(const Min *op) {
    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
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

void CodeGen_C::visit(const EQ *op) {
    visit_binop(op->type, op->a, op->b, "==");
}

void CodeGen_C::visit(const NE *op) {
    visit_binop(op->type, op->a, op->b, "!=");
}

void CodeGen_C::visit(const LT *op) {
    visit_binop(op->type, op->a, op->b, "<");
}

void CodeGen_C::visit(const LE *op) {
    visit_binop(op->type, op->a, op->b, "<=");
}

void CodeGen_C::visit(const GT *op) {
    visit_binop(op->type, op->a, op->b, ">");
}

void CodeGen_C::visit(const GE *op) {
    visit_binop(op->type, op->a, op->b, ">=");
}

void CodeGen_C::visit(const Or *op) {
    visit_binop(op->type, op->a, op->b, "||");
}

void CodeGen_C::visit(const And *op) {
    visit_binop(op->type, op->a, op->b, "&&");
}

void CodeGen_C::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_C::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        id = std::to_string(op->value);
    } else {
        print_assignment(op->type, "(" + print_type(op->type) + ")(ADD_INT64_T_SUFFIX(" + std::to_string(op->value) + "))");
    }
}

void CodeGen_C::visit(const UIntImm *op) {
    print_assignment(op->type, "(" + print_type(op->type) + ")(ADD_UINT64_T_SUFFIX(" + std::to_string(op->value) + "))");
}

void CodeGen_C::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

// NaN is the only float/double for which this is true... and
// surprisingly, there doesn't seem to be a portable isnan function
// (dsharlet).
template<typename T>
static bool isnan(T x) {
    return x != x;
}

template<typename T>
static bool isinf(T x) {
    return std::numeric_limits<T>::has_infinity && (x == std::numeric_limits<T>::infinity() ||
                                                    x == -std::numeric_limits<T>::infinity());
}

void CodeGen_C::visit(const FloatImm *op) {
    if (isnan(op->value)) {
        id = "nan_f32()";
    } else if (isinf(op->value)) {
        if (op->value > 0) {
            id = "inf_f32()";
        } else {
            id = "neg_inf_f32()";
        }
    } else {
        // Write the constant as reinterpreted uint to avoid any bits lost in conversion.
        union {
            uint32_t as_uint;
            float as_float;
        } u;
        u.as_float = op->value;

        ostringstream oss;
        if (op->type.bits() == 64) {
            oss << "(double) ";
        }
        oss << "float_from_bits(" << u.as_uint << "u /* " << u.as_float << " */)";
        print_assignment(op->type, oss.str());
    }
}

void CodeGen_C::visit(const Call *op) {

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
            rhs << "uint16x32_t_shift_right(" << a0 << ", " << a1 << ")";
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
    } else if (op->name == "halide_xtensa_clamped_dense_load_i16") {
        vector<string> args(op->args.size());
        args[0] = print_name(op->args[0].as<StringImm>()->value);
        for (size_t i = 1; i < op->args.size(); i++) {
            args[i] = print_expr(op->args[i]);
        }
        rhs << op->name << "(" << with_commas(args) << ")";
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

string CodeGen_C::print_scalarized_expr(const Expr &e) {
    Type t = e.type();
    internal_assert(t.is_vector());
    string v = unique_name('_');
    stream << get_indent() << print_type(t, AppendSpace) << v << ";\n";
    for (int lane = 0; lane < t.lanes(); lane++) {
        Expr e2 = extract_lane(e, lane);
        string elem = print_expr(e2);
        ostringstream rhs;
        rhs << v << ".replace(" << lane << ", " << elem << ")";
        v = print_assignment(t, rhs.str());
    }
    return v;
}

string CodeGen_C::print_extern_call(const Call *op) {
    if (op->type.is_vector()) {
        // Need to split into multiple scalar calls.
        return print_scalarized_expr(op);
    }
    ostringstream rhs;
    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
        // This substitution ensures const correctness for all calls
        if (args[i] == "__user_context") {
            args[i] = "_ucon";
        }
    }
    if (function_takes_user_context(op->name)) {
        args.insert(args.begin(), "_ucon");
    }
    rhs << op->name << "(" << with_commas(args) << ")";
    return rhs.str();
}

string CodeGen_C::print_xtensa_call(const Call *op) {
    ostringstream rhs;
    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
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
    }

    rhs << op_name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_C::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "Predicated load is not supported by C backend.\n";

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
        if ((op->alignment.modulus % op->type.lanes() == 0) && (op->alignment.remainder % op->type.lanes() == 0)) {
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

void CodeGen_C::visit(const Store *op) {
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
        if ((op->alignment.modulus % op->value.type().lanes() == 0) && (op->alignment.remainder % op->value.type().lanes() == 0)) {
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

void CodeGen_C::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr body = op->body;
    if (op->value.type().is_handle()) {
        // The body might contain a Load that references this directly
        // by name, so we can't rewrite the name.
        stream << get_indent() << print_type(op->value.type())
               << " " << print_name(op->name)
               << " = " << id_value << ";\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    print_expr(body);
}

void CodeGen_C::visit(const Select *op) {
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
        if (op->type.is_int_or_uint() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            rhs << "IVP_MOVNX16T(" << true_val << ", " << false_val << ", " << cond << ")";
        } else {
            rhs << type << "::select(" << cond << ", " << true_val << ", " << false_val << ")";
        }
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Stmt body = op->body;
    if (op->value.type().is_handle()) {
        // The body might contain a Load or Store that references this
        // directly by name, so we can't rewrite the name.
        stream << get_indent() << print_type(op->value.type())
               << " " << print_name(op->name)
               << " = " << id_value << ";\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    body.accept(this);
}

// Halide asserts have different semantics to C asserts.  They're
// supposed to clean up and make the containing function return
// -1, so we can't use the C version of assert. Instead we convert
// to an if statement.
void CodeGen_C::create_assertion(const string &id_cond, const string &id_msg) {
    if (target.has_feature(Target::NoAsserts)) return;

    stream << get_indent() << "if (!" << id_cond << ")\n";
    open_scope();
    stream << get_indent() << "return " << id_msg << ";\n";
    close_scope("");
}

void CodeGen_C::create_assertion(const string &id_cond, const Expr &message) {
    internal_assert(!message.defined() || message.type() == Int(32))
        << "Assertion result is not an int: " << message;

    if (target.has_feature(Target::NoAsserts)) {
        stream << get_indent() << "(void)" << id_cond << ";\n";
        return;
    }

    // don't call the create_assertion(string, string) version because
    // we don't want to force evaluation of 'message' unless the condition fails
    stream << get_indent() << "if (!" << id_cond << ") ";
    open_scope();
    string id_msg = print_expr(message);
    stream << get_indent() << "return " << id_msg << ";\n";
    close_scope("");
}

void CodeGen_C::create_assertion(const Expr &cond, const Expr &message) {
    create_assertion(print_expr(cond), message);
}

void CodeGen_C::visit(const AssertStmt *op) {
    create_assertion(op->condition, op->message);
}

void CodeGen_C::visit(const ProducerConsumer *op) {
    stream << get_indent();
    if (op->is_producer) {
        stream << "// produce " << op->name << "\n";
    } else {
        stream << "// consume " << op->name << "\n";
    }
    print_stmt(op->body);
}

void CodeGen_C::visit(const Fork *op) {
    // TODO: This doesn't actually work with nested tasks
    stream << get_indent() << "#pragma omp parallel\n";
    open_scope();
    stream << get_indent() << "#pragma omp single\n";
    open_scope();
    stream << get_indent() << "#pragma omp task\n";
    open_scope();
    print_stmt(op->first);
    close_scope("");
    stream << get_indent() << "#pragma omp task\n";
    open_scope();
    print_stmt(op->rest);
    close_scope("");
    stream << get_indent() << "#pragma omp taskwait\n";
    close_scope("");
    close_scope("");
}

void CodeGen_C::visit(const Acquire *op) {
    string id_sem = print_expr(op->semaphore);
    string id_count = print_expr(op->count);
    open_scope();
    stream << get_indent() << "while (!halide_semaphore_try_acquire(" << id_sem << ", " << id_count << "))\n";
    open_scope();
    stream << get_indent() << "#pragma omp taskyield\n";
    close_scope("");
    op->body.accept(this);
    close_scope("");
}

void CodeGen_C::visit(const Atomic *op) {
    if (!op->mutex_name.empty()) {
        internal_assert(!inside_atomic_mutex_node)
            << "Nested atomic mutex locks detected. This might causes a deadlock.\n";
        ScopedValue<bool> old_inside_atomic_mutex_node(inside_atomic_mutex_node, true);
        op->body.accept(this);
    } else {
        // Issue atomic stores.
        ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
        op->body.accept(this);
    }
}

static int loop_level = 0;
void CodeGen_C::visit(const For *op) {
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

void CodeGen_C::visit(const Ramp *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);
    print_assignment(vector_type, print_type(vector_type) + "::ramp(" + id_base + ", " + id_stride + ")");
}

void CodeGen_C::visit(const Broadcast *op) {
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

void CodeGen_C::visit(const Provide *op) {
    internal_error << "Cannot emit Provide statements as C\n";
}

void CodeGen_C::visit(const Allocate *op) {
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
                   // << " __restrict "
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
        create_assertion(op_name, "halide_error_out_of_memory(_ucon)");

        stream << get_indent();
        string free_function = op->free_function.empty() ? "halide_free" : op->free_function;
        stream << "HalideFreeHelper " << op_name << "_free(_ucon, "
               << op_name << ", " << free_function << ");\n";
    }

    op->body.accept(this);

    // Should have been freed internally
    internal_assert(!allocations.contains(op->name));

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_C::visit(const Free *op) {
    if (heap_allocations.contains(op->name)) {
        stream << get_indent() << print_name(op->name) << "_free.free();\n";
        heap_allocations.pop(op->name);
    }
    allocations.pop(op->name);
}

void CodeGen_C::visit(const Realize *op) {
    internal_error << "Cannot emit realize statements to C\n";
}

void CodeGen_C::visit(const Prefetch *op) {
    internal_error << "Cannot emit prefetch statements to C\n";
}

void CodeGen_C::visit(const IfThenElse *op) {
    string cond_id = print_expr(op->condition);

    stream << get_indent() << "if (" << cond_id << ")\n";
    open_scope();
    op->then_case.accept(this);
    close_scope("if " + cond_id);

    if (op->else_case.defined()) {
        stream << get_indent() << "else\n";
        open_scope();
        op->else_case.accept(this);
        close_scope("if " + cond_id + " else");
    }
}

void CodeGen_C::visit(const Evaluate *op) {
    if (is_const(op->value)) return;
    string id = print_expr(op->value);
    stream << get_indent() << "halide_unused(" << id << ");\n";
}

void CodeGen_C::visit(const Shuffle *op) {
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
        string storage_name = unique_name('_');
        stream << get_indent() << "const " << print_type(op->vectors[0].type()) << " " << storage_name << "[] = { " << with_commas(vecs) << " };\n";

        rhs << print_type(op->type) << "::concat(" << op->vectors.size() << ", " << storage_name << ")";
        src = print_assignment(op->type, rhs.str());
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        rhs << src << "[" << op->indices[0] << "]";
    } else {
        string indices_name = unique_name('_');
        stream << get_indent() << "const int32_t " << indices_name << "[" << op->indices.size() << "] = { " << with_commas(op->indices) << " };\n";
        rhs << print_type(op->type) << "::shuffle(" << src << ", " << indices_name << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::test() {
    return;
    LoweredArgument buffer_arg("buf", Argument::OutputBuffer, Int(32), 3, ArgumentEstimates{});
    LoweredArgument float_arg("alpha", Argument::InputScalar, Float(32), 0, ArgumentEstimates{});
    LoweredArgument int_arg("beta", Argument::InputScalar, Int(32), 0, ArgumentEstimates{});
    LoweredArgument user_context_arg("__user_context", Argument::InputScalar, type_of<const void *>(), 0, ArgumentEstimates{});
    vector<LoweredArgument> args = {buffer_arg, float_arg, int_arg, user_context_arg};
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x, Parameter(), const_true(), ModulusRemainder());
    s = LetStmt::make("x", beta + 1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), MemoryType::Stack, {127}, const_true(), s);
    s = Block::make(s, Free::make("tmp.heap"));
    s = Allocate::make("tmp.heap", Int(32), MemoryType::Heap, {43, beta}, const_true(), s);
    Expr buf = Variable::make(Handle(), "buf.buffer");
    s = LetStmt::make("buf", Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern), s);

    Module m("", get_host_target());
    m.append(LoweredFunc("test1", args, s, LinkageType::External));

    ostringstream source;
    ostringstream macros;
    {
        CodeGen_C cg(source, Target("host"), CodeGen_C::CImplementation);
        cg.compile(m);
        cg.add_common_macros(macros);
    }

    string src = source.str();
    string correct_source =
        headers +
        globals +
        string((const char *)halide_internal_runtime_header_HalideRuntime_h) + '\n' +
        string((const char *)halide_internal_initmod_inlined_c) + '\n' +
        macros.str() + '\n' + kDefineMustUseResult + R"GOLDEN_CODE(
#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta, void const *__user_context) {
 void * const _ucon = const_cast<void *>(__user_context);
 void *_0 = _halide_buffer_get_host(_buf_buffer);
 void * _buf = _0;
 {
  int64_t _1 = 43;
  int64_t _2 = _1 * _beta;
  if ((_2 > ((int64_t(1) << 31) - 1)) || ((_2 * sizeof(int32_t )) > ((int64_t(1) << 31) - 1)))
  {
   halide_error(_ucon, "32-bit signed overflow computing size of allocation tmp.heap\n");
   return -1;
  } // overflow test tmp.heap
  int64_t _3 = _2;
  int32_t *_tmp_heap = (int32_t  *)halide_malloc(_ucon, sizeof(int32_t )*_3);
  if (!_tmp_heap)
  {
   return halide_error_out_of_memory(_ucon);
  }
  HalideFreeHelper _tmp_heap_free(_ucon, _tmp_heap, halide_free);
  {
   int32_t _tmp_stack[127];
   int32_t _4 = _beta + 1;
   int32_t _5;
   bool _6 = _4 < 1;
   if (_6)
   {
    char b0[1024];
    snprintf(b0, 1024, "%lld%s", (long long)(3), "\n");
    char const *_7 = b0;
    halide_print(_ucon, _7);
    int32_t _8 = 0;
    int32_t _9 = return_second(_8, 3);
    _5 = _9;
   } // if _6
   else
   {
    _5 = 3;
   } // if _6 else
   int32_t _10 = _5;
   float _11 = float_from_bits(1082130432 /* 4 */);
   bool _12 = _alpha > _11;
   int32_t _13 = (int32_t)(_12 ? _10 : 2);
   ((int32_t *)_buf)[_4] = _13;
  } // alloc _tmp_stack
  _tmp_heap_free.free();
 } // alloc _tmp_heap
 return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif

)GOLDEN_CODE";

    if (src != correct_source) {
        int diff = 0;
        while (src[diff] == correct_source[diff]) {
            diff++;
        }
        int diff_end = diff + 1;
        while (diff > 0 && src[diff] != '\n') {
            diff--;
        }
        while (diff_end < (int)src.size() && src[diff_end] != '\n') {
            diff_end++;
        }

        internal_error
            << "Correct source code:\n"
            << correct_source
            << "Actual source code:\n"
            << src
            << "Difference starts at:\n"
            << "Correct: " << correct_source.substr(diff, diff_end - diff) << "\n"
            << "Actual: " << src.substr(diff, diff_end - diff) << "\n";
    }

    std::cout << "CodeGen_C test passed\n";
}

}  // namespace Internal
}  // namespace Halide

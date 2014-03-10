#include <sstream>
#include <iostream>
#include <limits>

#include "CodeGen_C.h"
#include "CodeGen.h"
#include "CodeGen_Internal.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include "Lerp.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::map;

namespace {
const string buffer_t_definition =
    "#ifndef BUFFER_T_DEFINED\n"
    "#define BUFFER_T_DEFINED\n"
    "#include <stdint.h>\n"
    "typedef struct buffer_t {\n"
    "    uint64_t dev;\n"
    "    uint8_t* host;\n"
    "    int32_t extent[4];\n"
    "    int32_t stride[4];\n"
    "    int32_t min[4];\n"
    "    int32_t elem_size;\n"
    "    bool host_dirty;\n"
    "    bool dev_dirty;\n"
    "} buffer_t;\n"
    "#endif\n";

const string preamble =
    "#include <iostream>\n"
    "#include <math.h>\n"
    "#include <float.h>\n"
    "#include <assert.h>\n"
    "#include <string.h>\n"
    "#include <stdint.h>\n"
    "\n"
    "extern \"C\" void *halide_malloc(void *ctx, size_t);\n"
    "extern \"C\" void halide_free(void *ctx, void *ptr);\n"
    "extern \"C\" int halide_debug_to_file(void *ctx, const char *filename, void *data, int, int, int, int, int, int);\n"
    "extern \"C\" int halide_start_clock(void *ctx);\n"
    "extern \"C\" int64_t halide_current_time_ns(void *ctx);\n"
    "extern \"C\" uint64_t halide_profiling_timer(void *ctx);\n"
    "extern \"C\" int halide_printf(void *ctx, const char *fmt, ...);\n"
    "\n"

    // TODO: this next chunk is copy-pasted from posix_math.cpp. A
    // better solution for the C runtime would be nice.
    "#ifdef _WIN32\n"
    "extern \"C\" float roundf(float);\n"
    "extern \"C\" double round(double);\n"
    "#else\n"
    "inline float asinh_f32(float x) {return asinhf(x);}\n"
    "inline float acosh_f32(float x) {return acoshf(x);}\n"
    "inline float atanh_f32(float x) {return atanhf(x);}\n"
    "inline double asinh_f64(double x) {return asinh(x);}\n"
    "inline double acosh_f64(double x) {return acosh(x);}\n"
    "inline double atanh_f64(double x) {return atanh(x);}\n"
    "#endif\n"
    "inline float sqrt_f32(float x) {return sqrtf(x);}\n"
    "inline float sin_f32(float x) {return sinf(x);}\n"
    "inline float asin_f32(float x) {return asinf(x);}\n"
    "inline float cos_f32(float x) {return cosf(x);}\n"
    "inline float acos_f32(float x) {return acosf(x);}\n"
    "inline float tan_f32(float x) {return tanf(x);}\n"
    "inline float atan_f32(float x) {return atanf(x);}\n"
    "inline float sinh_f32(float x) {return sinhf(x);}\n"
    "inline float cosh_f32(float x) {return coshf(x);}\n"
    "inline float tanh_f32(float x) {return tanhf(x);}\n"
    "inline float hypot_f32(float x, float y) {return hypotf(x, y);}\n"
    "inline float exp_f32(float x) {return expf(x);}\n"
    "inline float log_f32(float x) {return logf(x);}\n"
    "inline float pow_f32(float x, float y) {return powf(x, y);}\n"
    "inline float floor_f32(float x) {return floorf(x);}\n"
    "inline float ceil_f32(float x) {return ceilf(x);}\n"
    "inline float round_f32(float x) {return roundf(x);}\n"
    "\n"
    "inline double sqrt_f64(double x) {return sqrt(x);}\n"
    "inline double sin_f64(double x) {return sin(x);}\n"
    "inline double asin_f64(double x) {return asin(x);}\n"
    "inline double cos_f64(double x) {return cos(x);}\n"
    "inline double acos_f64(double x) {return acos(x);}\n"
    "inline double tan_f64(double x) {return tan(x);}\n"
    "inline double atan_f64(double x) {return atan(x);}\n"
    "inline double sinh_f64(double x) {return sinh(x);}\n"
    "inline double cosh_f64(double x) {return cosh(x);}\n"
    "inline double tanh_f64(double x) {return tanh(x);}\n"
    "inline double hypot_f64(double x, double y) {return hypot(x, y);}\n"
    "inline double exp_f64(double x) {return exp(x);}\n"
    "inline double log_f64(double x) {return log(x);}\n"
    "inline double pow_f64(double x, double y) {return pow(x, y);}\n"
    "inline double floor_f64(double x) {return floor(x);}\n"
    "inline double ceil_f64(double x) {return ceil(x);}\n"
    "inline double round_f64(double x) {return round(x);}\n"
    "\n"
    "inline float maxval_f32() {return FLT_MAX;}\n"
    "inline float minval_f32() {return -FLT_MAX;}\n"
    "inline double maxval_f64() {return DBL_MAX;}\n"
    "inline double minval_f64() {return -DBL_MAX;}\n"
    "inline uint8_t maxval_u8() {return 0xff;}\n"
    "inline uint8_t minval_u8() {return 0;}\n"
    "inline uint16_t maxval_u16() {return 0xffff;}\n"
    "inline uint16_t minval_u16() {return 0;}\n"
    "inline uint32_t maxval_u32() {return 0xffffffff;}\n"
    "inline uint32_t minval_u32() {return 0;}\n"
    "inline uint64_t maxval_u64() {return 0xffffffffffffffff;}\n"
    "inline uint64_t minval_u64() {return 0;}\n"
    "inline int8_t maxval_s8() {return 0x7f;}\n"
    "inline int8_t minval_s8() {return 0x80;}\n"
    "inline int16_t maxval_s16() {return 0x7fff;}\n"
    "inline int16_t minval_s16() {return 0x8000;}\n"
    "inline int32_t maxval_s32() {return 0x7fffffff;}\n"
    "inline int32_t minval_s32() {return 0x80000000;}\n"
    "inline int64_t maxval_s64() {return 0x7fffffffffffffff;}\n"
    "inline int64_t minval_s64() {return 0x8000000000000000;}\n"
    "\n"
    "inline int8_t abs_i8(int8_t a) {return a >= 0 ? a : -a;}\n"
    "inline int16_t abs_i16(int16_t a) {return a >= 0 ? a : -a;}\n"
    "inline int32_t abs_i32(int32_t a) {return a >= 0 ? a : -a;}\n"
    "inline int64_t abs_i64(int64_t a) {return a >= 0 ? a : -a;}\n"
    "inline float abs_f32(float a) {return fabsf(a);}\n"
    "inline double abs_f64(double a) {return fabs(a);}\n"
    "\n"
    "inline float nan_f32() {return NAN;}\n"
    "inline float neg_inf_f32() {return -INFINITY;}\n"
    "inline float inf_f32() {return INFINITY;}\n"
    "inline float float_from_bits(uint32_t bits) {\n"
    " union {\n"
    "  uint32_t as_uint;\n"
    "  float as_float;\n"
    " } u;\n"
    " u.as_uint = bits;\n"
    " return u.as_float;\n"
    "}\n"
    "\n"
    "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
    "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n"
    "template<typename T> T mod(T a, T b) {T result = a % b; if (result < 0) result += b; return result;}\n"
    "template<typename T> T sdiv(T a, T b) {return (a - mod(a, b))/b;}\n"

    // This may look wasteful, but it's the right way to do
    // it. Compilers understand memcpy and will convert it to a no-op
    // when used in this way. See http://blog.regehr.org/archives/959
    // for a detailed comparison of type-punning methods.
    "template<typename A, typename B> A reinterpret(B b) {A a; memcpy(&a, &b, sizeof(a)); return a;}\n"
    "\n"
    + buffer_t_definition +
    "bool halide_rewrite_buffer(buffer_t *b, int32_t elem_size,\n"
    "                           int32_t min0, int32_t extent0, int32_t stride0,\n"
    "                           int32_t min1, int32_t extent1, int32_t stride1,\n"
    "                           int32_t min2, int32_t extent2, int32_t stride2,\n"
    "                           int32_t min3, int32_t extent3, int32_t stride3) {\n"
    " b->min[0] = min0;\n"
    " b->min[1] = min1;\n"
    " b->min[2] = min2;\n"
    " b->min[3] = min3;\n"
    " b->extent[0] = extent0;\n"
    " b->extent[1] = extent1;\n"
    " b->extent[2] = extent2;\n"
    " b->extent[3] = extent3;\n"
    " b->stride[0] = stride0;\n"
    " b->stride[1] = stride1;\n"
    " b->stride[2] = stride2;\n"
    " b->stride[3] = stride3;\n"
    " return true;\n"
    "}\n";
}

CodeGen_C::CodeGen_C(ostream &s) : IRPrinter(s), id("$$ BAD ID $$") {}

namespace {
string type_to_c_type(Type type) {
    ostringstream oss;
    assert(type.width == 1 && "Can't codegen vector types to C (yet)");
    if (type.is_float()) {
        if (type.bits == 32) {
            oss << "float";
        } else if (type.bits == 64) {
            oss << "double";
        } else {
            assert(false && "Can't represent a float with this many bits in C");
        }

    } else if (type.is_handle()) {
        oss << "void *";
    } else {
        switch (type.bits) {
        case 1:
            oss << "bool";
            break;
        case 8: case 16: case 32: case 64:
            if (type.is_uint()) oss << 'u';
            oss << "int" << type.bits << "_t";
            break;
        default:
            assert(false && "Can't represent an integer with this many bits in C");
        }
    }
    return oss.str();
}
}

string CodeGen_C::print_type(Type type) {
    return type_to_c_type(type);
}

string CodeGen_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    oss << "reinterpret<" << print_type(type) << ">(" << print_expr(e) << ")";
    return oss.str();
}

string CodeGen_C::print_name(const string &name) {
    ostringstream oss;
    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') oss << '_';
        else if (name[i] == '$') oss << "__";
        else oss << name[i];
    }
    return oss.str();
}

void CodeGen_C::compile_header(const string &name, const vector<Argument> &args) {
    stream << "#ifndef HALIDE_" << name << '\n'
           << "#define HALIDE_" << name << '\n';

    // Throw in a definition of a buffer_t
    stream << buffer_t_definition;

    // Throw in a default (empty) definition of HALIDE_FUNCTION_ATTRS
    // (some hosts may define this to e.g. __attribute__((warn_unused_result)))
    stream << "#ifndef HALIDE_FUNCTION_ATTRS\n";
    stream << "#define HALIDE_FUNCTION_ATTRS\n";
    stream << "#endif\n";

    // Now the function prototype
    stream << "extern \"C\" int " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) stream << ", ";
        if (args[i].is_buffer) {
            stream << "buffer_t *" << print_name(args[i].name);
        } else {
            stream << "const "
                   << print_type(args[i].type)
                   << " " << print_name(args[i].name);
        }
    }
    stream << ") HALIDE_FUNCTION_ATTRS;\n";

    stream << "#endif\n";
}

namespace {
class ExternCallPrototypes : public IRGraphVisitor {
    std::set<string> emitted;
    using IRGraphVisitor::visit;

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);

        if (op->call_type == Call::Extern) {
            if (!emitted.count(op->name)) {
                stream << "extern \"C\" " << type_to_c_type(op->type)
                       << " " << op->name << "(";
                for (size_t i = 0; i < op->args.size(); i++) {
                    if (i > 0) {
                        stream << ", ";
                    }
                    stream << type_to_c_type(op->args[i].type());
                }
                stream << ");\n";
                emitted.insert(op->name);
            }
        }
    }

public:
    ostream &stream;
    ExternCallPrototypes(ostream &s) : stream(s) {
        size_t j = 0;
        // Make sure we don't catch calls that are already in the preamble
        for (size_t i = 0; i < preamble.size(); i++) {
            char c = preamble[i];
            if (c == '(' && i > j+1) {
                // Could be the end of a function_name.
                emitted.insert(preamble.substr(j+1, i-j-1));
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
};
}

void CodeGen_C::compile(Stmt s, string name,
                        const vector<Argument> &args,
                        const vector<Buffer> &images_to_embed) {
    stream << preamble;

    // Embed the constant images
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        Buffer buffer = images_to_embed[i];
        string name = print_name(buffer.name());
        buffer_t b = *(buffer.raw_buffer());

        // Figure out the offset of the last pixel.
        size_t num_elems = 1;
        for (int d = 0; b.extent[d]; d++) {
            num_elems += b.stride[d] * (b.extent[d] - 1);
        }

        // Emit the data
        stream << "static uint8_t " << name << "_data[] __attribute__ ((aligned (32))) = {";
        for (size_t i = 0; i < num_elems * b.elem_size; i++) {
            if (i > 0) stream << ", ";
            stream << (int)(b.host[i]);
        }
        stream << "};\n";

        // Emit the buffer_t
        assert(b.host && "Can't embed an image with a null host pointer\n");
        assert(!b.dev_dirty && "Can't embed an image with a dirty device pointer\n");
        stream << "static buffer_t " << name << "_buffer = {"
               << "0, " // dev
               << "&" << name << "_data[0], " // host
               << "{" << b.extent[0] << ", " << b.extent[1] << ", " << b.extent[2] << ", " << b.extent[3] << "}, "
               << "{" << b.stride[0] << ", " << b.stride[1] << ", " << b.stride[2] << ", " << b.stride[3] << "}, "
               << "{" << b.min[0] << ", " << b.min[1] << ", " << b.min[2] << ", " << b.min[3] << "}, "
               << b.elem_size << ", "
               << "0, " // host_dirty
               << "0};\n"; //dev_dirty

        // Make a global pointer to it
        stream << "static buffer_t *_" << name << " = &" << name << "_buffer;\n";

    }

    have_user_context = false;
    for (size_t i = 0; i < args.size(); i++) {
        // TODO: check that its type is void *?
        have_user_context |= (args[i].name == "__user_context");
    }

    // Emit prototypes for any extern calls used.
    {
        stream << "\n";
        ExternCallPrototypes e(stream);
        s.accept(&e);
        stream << "\n";
    }

    // Emit the function prototype
    stream << "extern \"C\" int " << name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "buffer_t *_"
                   << print_name(args[i].name);
        } else {
            stream << "const "
                   << print_type(args[i].type)
                   << " "
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }

    stream << ") {\n";

    // Unpack the buffer_t's
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            unpack_buffer(args[i].type, args[i].name);
        }
    }
    for (size_t i = 0; i < images_to_embed.size(); i++) {
        unpack_buffer(images_to_embed[i].type(), images_to_embed[i].name());
    }
    // Emit the body
    print(s);

    stream << "return 0;\n"
           << "}\n";
}

void CodeGen_C::unpack_buffer(Type t, const std::string &buffer_name) {
    string name = print_name(buffer_name);
    string type = print_type(t);
    stream << type
           << " *"
           << name
           << " = ("
           << type
           << " *)(_"
           << name
           << "->host);\n";
    allocations.push(buffer_name, t);

    stream << "const bool "
           << name
           << "_host_and_dev_are_null = (_"
           << name << "->host == NULL) && (_"
           << name << "->dev == 0);\n";
    stream << "(void)" << name << "_host_and_dev_are_null;\n";

    for (int j = 0; j < 4; j++) {
        stream << "const int32_t "
               << name
               << "_min_" << j << " = _"
               << name
               << "->min[" << j << "];\n";
        // emit a void cast to suppress "unused variable" warnings
        stream << "(void)" << name << "_min_" << j << ";\n";
    }
    for (int j = 0; j < 4; j++) {
        stream << "const int32_t "
               << name
               << "_extent_" << j << " = _"
               << name
               << "->extent[" << j << "];\n";
        stream << "(void)" << name << "_extent_" << j << ";\n";
    }
    for (int j = 0; j < 4; j++) {
        stream << "const int32_t "
               << name
               << "_stride_" << j << " = _"
               << name
               << "->stride[" << j << "];\n";
        stream << "(void)" << name << "_stride_" << j << ";\n";
    }
    stream << "const int32_t "
           << name
           << "_elem_size = _"
           << name
           << "->elem_size;\n";
}

string CodeGen_C::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

void CodeGen_C::print_stmt(Stmt s) {
    s.accept(this);
}

string CodeGen_C::print_assignment(Type t, const std::string &rhs) {

    map<string, string>::iterator cached = cache.find(rhs);

    if (cached == cache.end()) {
        id = unique_name('V');
        do_indent();
        stream << print_type(t)
               << " " << id
               << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_C::open_scope() {
    cache.clear();
    do_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_C::close_scope(const std::string &comment) {
    cache.clear();
    indent--;
    do_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

void CodeGen_C::visit(const Variable *op) {
    ostringstream oss;
    for (size_t i = 0; i < op->name.size(); i++) {
        if (op->name[i] == '.') oss << '_';
        else if (op->name[i] == '$') oss << "__";
        else oss << op->name[i];
    }
    id = oss.str();
}

void CodeGen_C::visit(const Cast *op) {
    print_assignment(op->type, "(" + print_type(op->type) + ")(" + print_expr(op->value) + ")");
}

void CodeGen_C::visit_binop(Type t, Expr a, Expr b, const char * op) {
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
    visit_binop(op->type, op->a, op->b, "*");
}

void CodeGen_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(Call::make(op->type, "sdiv", vec(op->a, op->b), Call::Extern));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits)-1);
        print_assignment(op->type, oss.str());
    } else {
        print_expr(Call::make(op->type, "mod", vec(op->a, op->b), Call::Extern));
    }
}

void CodeGen_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", vec(op->a, op->b), Call::Extern));
}

void CodeGen_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", vec(op->a, op->b), Call::Extern));
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
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_C::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

// NaN is the only float/double for which this is true... and
// surprisingly, there doesn't seem to be a portable isnan function
// (dsharlet).
template <typename T>
static bool isnan(T x) { return x != x; }

template <typename T>
static bool isinf(T x)
{
    return std::numeric_limits<T>::has_infinity && (
        x == std::numeric_limits<T>::infinity() ||
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
        oss << "float_from_bits(" << u.as_uint << " /* " << u.as_float << " */)";
        id = oss.str();
    }
}

void CodeGen_C::visit(const Call *op) {

    assert((op->call_type == Call::Extern || op->call_type == Call::Intrinsic) &&
           "Can only codegen extern calls and intrinsics");

    ostringstream rhs;

    // Handle intrinsics first
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::debug_to_file) {
            assert(op->args.size() == 9);
            const StringImm *string_imm = op->args[0].as<StringImm>();
            assert(string_imm);
            string filename = string_imm->value;
            const Load *load = op->args[1].as<Load>();
            assert(load);
            string func = load->name;

            vector<string> args(6);
            for (size_t i = 0; i < args.size(); i++) {
                args[i] = print_expr(op->args[i+3]);
            }

            rhs << "halide_debug_to_file(";
            rhs << (have_user_context ? "__user_context" : "NULL");
            rhs << ", \"" + filename + "\", " + func;
            for (size_t i = 0; i < args.size(); i++) {
                rhs << ", " << args[i];
            }
            rhs << ")";
        } else if (op->name == Call::bitwise_and) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " & " << a1;
        } else if (op->name == Call::bitwise_xor) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " ^ " << a1;
        } else if (op->name == Call::bitwise_or) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " | " << a1;
        } else if (op->name == Call::bitwise_not) {
            assert(op->args.size() == 1);
            rhs << "~" << print_expr(op->args[0]);
        } else if (op->name == Call::reinterpret) {
            assert(op->args.size() == 1);
            rhs << print_reinterpret(op->type, op->args[0]);
        } else if (op->name == Call::shift_left) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " << " << a1;
        } else if (op->name == Call::shift_right) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " >> " << a1;
        } else if (op->name == Call::rewrite_buffer) {
            int dims = ((int)(op->args.size())-2)/3;
            assert((int)(op->args.size()) == dims*3 + 2);
            assert(dims <= 4);
            vector<string> args(op->args.size());
            const Variable *v = op->args[0].as<Variable>();
            assert(v && ends_with(v->name, ".buffer"));
            args[0] = "_" + v->name.substr(0, v->name.size() - 7);
            for (size_t i = 1; i < op->args.size(); i++) {
                args[i] = print_expr(op->args[i]);
            }
            rhs << "halide_rewrite_buffer(";
            for (size_t i = 0; i < 14; i++) {
                if (i > 0) rhs << ", ";
                if (i < args.size()) {
                    rhs << args[i];
                } else {
                    rhs << '0';
                }
            }
            rhs << ")";
        } else if (op->name == Call::profiling_timer) {
            assert(op->args.size() == 0);
            rhs << "halide_profiling_timer(";
            rhs << (have_user_context ? "__user_context" : "NULL");
            rhs << ")";
        } else if (op->name == Call::lerp) {
            Expr e = lower_lerp(op->args[0], op->args[1], op->args[2]);
            rhs << print_expr(e);
        } else if (op->name == Call::null_handle) {
            rhs << "NULL";
        } else if (op->name == Call::address_of) {
            const Load *l = op->args[0].as<Load>();
            assert(op->args.size() == 1 && l);
            rhs << "(("
                << print_type(l->type)
                << " *)"
                << print_name(l->name)
                << " + "
                << print_expr(l->index)
                << ")";
        } else if (op->name == Call::return_second) {
            assert(op->args.size() == 2);
            string arg0 = print_expr(op->args[0]);
            string arg1 = print_expr(op->args[1]);
            rhs << "(" << arg0 << ", " << arg1 << ")";
        } else if (op->name == Call::if_then_else) {
            assert(op->args.size() == 3);

            string result_id = unique_name('V');

            do_indent();
            stream << print_type(op->args[1].type())
                   << " " << result_id << ";\n";

            string cond_id = print_expr(op->args[0]);

            do_indent();
            stream << "if (" << cond_id << ")\n";
            open_scope();
            string true_case = print_expr(op->args[1]);
            do_indent();
            stream << result_id << " = " << true_case << ";\n";
            close_scope("if " + cond_id);
            do_indent();
            stream << "else\n";
            open_scope();
            string false_case = print_expr(op->args[2]);
            do_indent();
            stream << result_id << " = " << false_case << ";\n";
            close_scope("if " + cond_id + " else");

            rhs << result_id;
        } else if (op->name == Call::create_buffer_t) {
            assert(op->args.size() >= 2);
            vector<string> args;
            for (size_t i = 0; i < op->args.size(); i++) {
                args.push_back(print_expr(op->args[i]));
            }
            string buf_id = unique_name('B');
            do_indent();
            stream << "buffer_t " << buf_id << " = {0};\n";
            do_indent();
            stream << buf_id << ".host = (uint8_t *)(" << args[0] << ");\n";
            do_indent();
            stream << buf_id << ".elem_size = " << args[1] << ";\n";
            int dims = ((int)op->args.size() - 2)/3;
            for (int i = 0; i < dims; i++) {
                do_indent();
                stream << buf_id << ".min[" << i << "] = " << args[i*3+2] << ";\n";
                do_indent();
                stream << buf_id << ".extent[" << i << "] = " << args[i*3+3] << ";\n";
                do_indent();
                stream << buf_id << ".stride[" << i << "] = " << args[i*3+4] << ";\n";
            }
            rhs << "(&" + buf_id + ")";
        } else if (op->name == Call::extract_buffer_extent) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << "((buffer_t *)(" << a0 << "))->extent[" << a1 << "]";
        } else if (op->name == Call::extract_buffer_min) {
            assert(op->args.size() == 2);
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << "((buffer_t *)(" << a0 << "))->min[" << a1 << "]";
        } else if (op->name == Call::abs) {
            assert(op->args.size() == 1);
            string arg = print_expr(op->args[0]);
            rhs << "(" << arg << " > 0 ? " << arg << " : -" << arg << ")";
        } else {
          // TODO: other intrinsics
          std::cerr << "Unhandled intrinsic: " << op->name << '\n';
          assert(false);
        }

    } else {
        // Generic calls
        vector<string> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            args[i] = print_expr(op->args[i]);
        }
        rhs << print_name(op->name) << "(";

        if (CodeGen::function_takes_user_context(op->name)) {
            rhs << (have_user_context ? "__user_context, " : "NULL, ");
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) rhs << ", ";
            rhs << args[i];
        }
        rhs << ")";
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Load *op) {
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name) == op->type);
    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "(("
            << print_type(op->type)
            << " *)"
            << print_name(op->name)
            << ")";
    } else {
        rhs << print_name(op->name);
    }
    rhs << "["
        << print_expr(op->index)
        << "]";

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Store *op) {

    Type t = op->value.type();

    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name) == t);

    string id_index = print_expr(op->index);
    string id_value = print_expr(op->value);
    do_indent();

    if (type_cast_needed) {
        stream << "(("
               << print_type(t)
               << " *)"
               << print_name(op->name)
               << ")";
    } else {
        stream << print_name(op->name);
    }
    stream << "["
           << id_index
           << "] = "
           << id_value
           << ";\n";
}

void CodeGen_C::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Expr body = substitute(op->name, new_var, op->body);
    print_expr(body);
}

void CodeGen_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    rhs << "(" << print_type(op->type) << ")"
        << "(" << cond
        << " ? " << true_val
        << " : " << false_val
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Stmt body = substitute(op->name, new_var, op->body);
    body.accept(this);
}

void CodeGen_C::visit(const AssertStmt *op) {
    string id_cond = print_expr(op->condition);

    vector<string> id_args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        id_args[i] = print_expr(op->args[i]);
    }

    do_indent();
    // Halide asserts have different semantics to C asserts. The
    // conditions sometimes contain necessary side-effects, and
    // they're supposed to make the containing function return -1, so
    // we can't use the C version of assert. Instead we convert to an
    // if statement.

    stream << "if (!" << id_cond << ") {\n";
    do_indent();
    stream << " halide_printf("
           << (have_user_context ? "__user_context, " : "NULL, ")
           << Expr(op->message + "\n");
    for (size_t i = 0; i < op->args.size(); i++) {
        stream << ", " << id_args[i];
    }
    stream << ");\n";
    do_indent();
    stream << " return -1;\n";
    do_indent();
    stream << "}\n";
}

void CodeGen_C::visit(const Pipeline *op) {

    do_indent();
    stream << "// produce " << op->name << '\n';
    print_stmt(op->produce);

    if (op->update.defined()) {
        do_indent();
        stream << "// update " << op->name << '\n';
        print_stmt(op->update);
    }

    do_indent();
    stream << "// consume " << op->name << '\n';
    print_stmt(op->consume);
}

void CodeGen_C::visit(const For *op) {
    if (op->for_type == For::Parallel) {
        do_indent();
        stream << "#pragma omp parallel for\n";
    } else {
        assert(op->for_type == For::Serial && "Can only emit serial or parallel for loops to C");
    }

    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    do_indent();
    stream << "for (int "
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

}

void CodeGen_C::visit(const Provide *op) {
    assert(false && "Cannot emit Provide statements as C");
}

void CodeGen_C::visit(const Allocate *op) {
    open_scope();

    // For sizes less than 8k, do a stack allocation
    bool on_stack = false;
    int32_t constant_size;
    string size_id;
    if (constant_allocation_size(op->extents, op->name, constant_size)) {
        int64_t stack_bytes = constant_size;

        if ((stack_bytes * op->type.bytes()) > ((int64_t(1) << 31) - 1)) {
            std::cerr << "Total size for allocation " << op->name << " is constant but exceeds 2^31 - 1.";
            assert(false);
        } else {
            size_id = print_expr(Expr(static_cast<int32_t>(stack_bytes)));
            if (stack_bytes <= 1024 * 8) {
                on_stack = true;
            }
        }
    } else {
        assert(op->extents.size() > 0); // Otherwise allocation is constant and zero sized.

        size_id = print_assignment(Int(64), print_expr(op->extents[0]));

        for (size_t i = 1; i < op->extents.size(); i++) {
            // Make the code a little less cluttered for two-dimensional case
            string new_size_id_rhs;
            string next_extent = print_expr(op->extents[i]);
            if (i > 1) {
                new_size_id_rhs =  "(" + size_id + " > ((int64_t(1) << 31) - 1)) ? " + size_id + " : (" + size_id + " * " + next_extent + ")";
            } else {
                new_size_id_rhs = size_id + " * " + next_extent;
            }
            size_id = print_assignment(Int(64), new_size_id_rhs);
        }
        do_indent();
        stream << "if ((" << size_id << " > ((int64_t(1) << 31) - 1)) || ((" << size_id <<
          " * sizeof(" << print_type(op->type) << ")) > ((int64_t(1) << 31) - 1)))\n";
        open_scope();
        do_indent();
        stream << "halide_printf("
               << (have_user_context ? "__user_context" : "NULL")
               << ", \"32-bit signed overflow computing size of allocation "
               << op->name << "\\n\");\n";
        close_scope("overflow test " + op->name);
    }

    allocations.push(op->name, op->type);

    do_indent();
    stream << print_type(op->type) << ' ';

    if (on_stack) {
        stream << print_name(op->name)
               << "[" << size_id << "];\n";
    } else {
        stream << "*"
               << print_name(op->name)
               << " = ("
               << print_type(op->type)
               << " *)halide_malloc("
               << (have_user_context ? "__user_context" : "NULL")
               << ", sizeof("
               << print_type(op->type)
               << ")*" << size_id << ");\n";
        heap_allocations.push(op->name, 0);
    }

    op->body.accept(this);

    // Should have been freed internally
    assert(!allocations.contains(op->name));

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_C::visit(const Free *op) {
    if (heap_allocations.contains(op->name)) {
        do_indent();
        stream << "halide_free("
               << (have_user_context ? "__user_context, " : "NULL, ")
               << print_name(op->name)
               << ");\n";
        heap_allocations.pop(op->name);
    }
    allocations.pop(op->name);
}

void CodeGen_C::visit(const Realize *op) {
    assert(false && "Cannot emit realize statements to C");
}

void CodeGen_C::visit(const IfThenElse *op) {
    do_indent();
    string cond_id = print_expr(op->condition);

    stream << "if (" << cond_id << ")\n";
    open_scope();
    op->then_case.accept(this);
    close_scope("if " + cond_id);

    if (op->else_case.defined()) {
        do_indent();
        stream << "else\n";
        open_scope();
        op->else_case.accept(this);
        close_scope("if " + cond_id + " else");
    }
}

void CodeGen_C::visit(const Evaluate *op) {
    string id = print_expr(op->value);
    do_indent();
    stream << "(void)" << id << ";\n";
}

void CodeGen_C::test() {
    Argument buffer_arg("buf", true, Int(32));
    Argument float_arg("alpha", false, Float(32));
    Argument int_arg("beta", false, Int(32));
    Argument user_context_arg("__user_context", false, Handle());
    vector<Argument> args(4);
    args[0] = buffer_arg;
    args[1] = float_arg;
    args[2] = int_arg;
    args[3] = user_context_arg;
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x);
    s = LetStmt::make("x", beta+1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), vec(Expr(127)), s);
    s = Block::make(s, Free::make("tmp.heap"));
    s = Allocate::make("tmp.heap", Int(32), vec(Expr(43), Expr(beta)), s);

    ostringstream source;
    CodeGen_C cg(source);
    cg.compile(s, "test1", args, vector<Buffer>());

    string correct_source = preamble +
        "\n\n"
        "extern \"C\" int test1(buffer_t *_buf, const float alpha, const int32_t beta, const void * __user_context) {\n"
        "int32_t *buf = (int32_t *)(_buf->host);\n"
        "const bool buf_host_and_dev_are_null = (_buf->host == NULL) && (_buf->dev == 0);\n"
        "(void)buf_host_and_dev_are_null;\n"
        "const int32_t buf_min_0 = _buf->min[0];\n"
        "(void)buf_min_0;\n"
        "const int32_t buf_min_1 = _buf->min[1];\n"
        "(void)buf_min_1;\n"
        "const int32_t buf_min_2 = _buf->min[2];\n"
        "(void)buf_min_2;\n"
        "const int32_t buf_min_3 = _buf->min[3];\n"
        "(void)buf_min_3;\n"
        "const int32_t buf_extent_0 = _buf->extent[0];\n"
        "(void)buf_extent_0;\n"
        "const int32_t buf_extent_1 = _buf->extent[1];\n"
        "(void)buf_extent_1;\n"
        "const int32_t buf_extent_2 = _buf->extent[2];\n"
        "(void)buf_extent_2;\n"
        "const int32_t buf_extent_3 = _buf->extent[3];\n"
        "(void)buf_extent_3;\n"
        "const int32_t buf_stride_0 = _buf->stride[0];\n"
        "(void)buf_stride_0;\n"
        "const int32_t buf_stride_1 = _buf->stride[1];\n"
        "(void)buf_stride_1;\n"
        "const int32_t buf_stride_2 = _buf->stride[2];\n"
        "(void)buf_stride_2;\n"
        "const int32_t buf_stride_3 = _buf->stride[3];\n"
        "(void)buf_stride_3;\n"
        "const int32_t buf_elem_size = _buf->elem_size;\n"
        "{\n"
        " int64_t V0 = 43;\n"
        " int64_t V1 = V0 * beta;\n"
        " if ((V1 > ((int64_t(1) << 31) - 1)) || ((V1 * sizeof(int32_t)) > ((int64_t(1) << 31) - 1)))\n"
        " {\n"
        "  halide_printf(\"32-bit signed overflow computing size of allocation tmp.heap\\n\");\n"
        " } // overflow test tmp.heap\n"
        " int32_t *tmp_heap = (int32_t *)halide_malloc(__user_context, sizeof(int32_t)*V1);\n"
        " {\n"
        "  int32_t tmp_stack[127];\n"
        "  int32_t V2 = beta + 1;\n"
        "  int32_t V3;\n"
        "  bool V4 = V2 < 1;\n"
        "  if (V4)\n"
        "  {\n"
        "   int64_t V5 = (int64_t)(3);\n"
        "   int32_t V6 = halide_printf(__user_context, \"%lld \\n\", V5);\n"
        "   int32_t V7 = (V6, 3);\n"
        "   V3 = V7;\n"
        "  } // if V4\n"
        "  else\n"
        "  {\n"
        "   V3 = 3;\n"
        "  } // if V4 else\n"
        "  int32_t V8 = V3;\n"
        "  bool V9 = alpha > float_from_bits(1082130432 /* 4 */);\n"
        "  int32_t V10 = (int32_t)(V9 ? V8 : 2);\n"
        "  buf[V2] = V10;\n"
        " } // alloc tmp_stack\n"
        " halide_free(__user_context, tmp_heap);\n"
        "} // alloc tmp_heap\n"
        "return 0;\n"
        "}\n";
    if (source.str() != correct_source) {
        std::cout << "Correct source code:\n" << correct_source;
        std::cout << "Actual source code:\n" << source.str();
        assert(false);
    }
    std::cout << "CodeGen_C test passed\n";
}

}
}

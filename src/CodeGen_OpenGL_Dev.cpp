#include "CodeGen_OpenGL_Dev.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Debug.h"
#include "Simplify.h"
#include <iomanip>
#include <map>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;

namespace {

std::string replace_all(std::string &str,
                        const std::string &find,
                        const std::string &replace) {
    size_t pos = 0;
    while ((pos = str.find(find, pos)) != std::string::npos) {
        str.replace(pos, find.length(), replace);
        pos += replace.length();
    }
    return str;
}

// Maps Halide types to appropriate GLSL types or emits error if no equivalent
// type is available.
Type map_type(const Type &type) {
    Type result = type;
    if (type.is_scalar()) {
        if (type.is_float()) {
            user_assert(type.bits <= 32)
                << "GLSL: Can't represent a float with " << type.bits << " bits.\n";
            result = Float(32);
        } else if (type.bits == 1) {
            result = Bool();
        } else if (type == Int(32)) {
            // Keep unchanged
        } else if (type == UInt(32)) {
            // GLSL doesn't have unsigned types, simply use int.
            result = Int(32);
        } else if (type.bits <= 16) {
            // Embed all other ints in a GLSL float. Probably not actually
            // valid for uint16 on systems with low float precision.
            result = Float(32);
        } else {
            user_error << "GLSL: Can't represent type '"<< type << "'.\n";
        }
    } else {
        user_assert(type.width <= 4)
            << "GLSL: vector types wider than 4 aren't supported\n";
        user_assert(type.is_bool() || type.is_int() || type.is_uint() || type.is_float())
            << "GLSL: Can't represent vector type '"<< type << "'.\n";
        Type scalar_type = type;
        scalar_type.width = 1;
        result = map_type(scalar_type);
        result.width = type.width;
    }
    return result;
}

// Most GLSL builtins are only defined for float arguments, so we may have to
// introduce type casts around the arguments and the entire function call.
Expr call_builtin(const Type &result_type, const std::string &func,
                  const std::vector<Expr> &args) {
    Type float_type = Float(32, result_type.width);
    std::vector<Expr> new_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].type().is_float()) {
            new_args[i] = Cast::make(float_type, args[i]);
        } else {
            new_args[i] = args[i];
        }
    }
    Expr val = Call::make(float_type, func, new_args, Call::Extern);
    return simplify(Cast::make(result_type, val));
}

}

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev(const Target &target)
    : target(target) {
    debug(1) << "Creating GLSL codegen\n";
    glc = new CodeGen_GLSL(src_stream);
}

CodeGen_OpenGL_Dev::~CodeGen_OpenGL_Dev() {
    delete glc;
}

void CodeGen_OpenGL_Dev::add_kernel(Stmt s, const string &name,
                                    const vector<GPU_Argument> &args) {
    cur_kernel_name = name;
    glc->compile(s, name, args, target);
}

void CodeGen_OpenGL_Dev::init_module() {
    src_stream.str("");
    src_stream.clear();
    cur_kernel_name = "";
}

vector<char> CodeGen_OpenGL_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "GLSL source:\n" << str << '\n';
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenGL_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenGL_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

//
// CodeGen_GLSL
//

CodeGen_GLSL::CodeGen_GLSL(std::ostream &s) : CodeGen_C(s) {
    builtin["sin_f32"] = "sin";
    builtin["sqrt_f32"] = "sqrt";
    builtin["cos_f32"] = "cos";
    builtin["exp_f32"] = "exp";
    builtin["log_f32"] = "log";
    builtin["abs_f32"] = "abs";
    builtin["floor_f32"] = "floor";
    builtin["ceil_f32"] = "ceil";
    builtin["pow_f32"] = "pow";
    builtin["asin_f32"] = "asin";
    builtin["acos_f32"] = "acos";
    builtin["tan_f32"] = "tan";
    builtin["atan_f32"] = "atan";
    builtin["atan2_f32"] = "atan"; // also called atan in GLSL
    builtin["min"] = "min";
    builtin["max"] = "max";
    builtin["mix"] = "mix";
    builtin["mod"] = "mod";
    builtin["abs"] = "abs";
}

string CodeGen_GLSL::print_type(Type type) {
    ostringstream oss;
    type = map_type(type);
    if (type.is_scalar()) {
        if (type.is_float()) {
            oss << "float";
        } else if (type.is_bool()) {
            oss << "bool";
        } else if (type.is_int()) {
            oss << "int";
        } else {
            internal_error << "GLSL: invalid type '" << type << "' encountered.\n";
        }
    } else {
        if (type.is_float()) {
            // no prefix for float vectors
        } else if (type.is_bool()) {
            oss << "b";
        } else if (type.is_int()) {
            oss << "i";
        } else {
            internal_error << "GLSL: invalid type '" << type << "' encountered.\n";
        }
        oss << "vec" << type.width;
    }
    return oss.str();
}

// Identifiers containing double underscores '__' are reserved in GLSL, so we
// have to use a different name mangling scheme than in the C code generator.
std::string CodeGen_GLSL::print_name(const std::string &name) {
    std::string mangled = CodeGen_C::print_name(name);
    return replace_all(mangled, "__", "XX");
}

void CodeGen_GLSL::visit(const FloatImm *op) {
    // TODO(dheck): use something like dtoa to avoid precision loss in
    // float->decimal conversion
    ostringstream oss;
    oss << std::showpoint << std::setprecision(8) << op->value;
    id = oss.str();
}

void CodeGen_GLSL::visit(const Cast *op) {
    Type value_type = op->value.type();
    // If both types are represented by the same GLSL type, no explicit cast
    // is necessary.
    if (map_type(op->type) == map_type(value_type)) {
        Expr value = op->value;
        if (value_type.code == Type::Float) {
            // float->int conversions may need explicit truncation if the
            // integer types is embedded into floats.  (Note: overflows are
            // considered undefined behavior, so we do nothing about values
            // that are out of range of the target type.)
            if (op->type.code == Type::UInt) {
                value = simplify(floor(value));
            } else if (op->type.code == Type::Int) {
                value = simplify(trunc(value));
            }
        }
        value.accept(this);
        return;
    } else {
        Type target_type = map_type(op->type);
        print_assignment(target_type, print_type(target_type) + "(" + print_expr(op->value) + ")");
    }
}

void CodeGen_GLSL::visit(const For *loop) {
    if (ends_with(loop->name, ".__block_id_x") ||
        ends_with(loop->name, ".__block_id_y")) {
        debug(1) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";

        string idx;
        if (ends_with(loop->name, ".__block_id_x")) {
            idx = "int(pixcoord.x)";
        } else if (ends_with(loop->name, ".__block_id_y")) {
            idx = "int(pixcoord.y)";
        }
        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name) << " = " << idx << ";\n";
        loop->body.accept(this);
    } else {
        user_assert(loop->for_type != For::Parallel) << "GLSL: parallel loops aren't allowed inside kernel.\n";
        CodeGen_C::visit(loop);
    }
}

class EvaluateSelect : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Ramp *op) {
        result.resize(op->width);
        for (int i = 0; i < op->width; i++) {
            result[i] = simplify(Add::make(op->base, Mul::make(op->stride, Expr(i))));
        }
    }

    void visit(const Broadcast *op) {
        result.resize(op->width);
        for (int i = 0; i < op->width; i++) {
            result[i] = op->value;
        }
    }

    template <class T>
    void visit_binary_op(const T *op) {
        op->a.accept(this);
        std::vector<Expr> result_a = result;
        op->b.accept(this);
        std::vector<Expr> result_b = result;
        for (size_t i = 0; i < result_a.size(); i++) {
            result[i] = simplify(T::make(result_a[i], result_b[i]));
        }
    }

    void visit(const EQ *op) {
        visit_binary_op(op);
    }

    void visit(const NE *op) {
        visit_binary_op(op);
    }

    void visit(const LT *op) {
        visit_binary_op(op);
    }

    void visit(const LE *op) {
        visit_binary_op(op);
    }

    void visit(const GT *op) {
        visit_binary_op(op);
    }

    void visit(const GE *op) {
        visit_binary_op(op);
    }

    void visit(const Add *op) {
        visit_binary_op(op);
    }

    void visit(const Sub *op) {
        visit_binary_op(op);
    }

    void visit(const Mul *op) {
        visit_binary_op(op);
    }

    void visit(const Div *op) {
        visit_binary_op(op);
    }

    void visit(const Min *op) {
        visit_binary_op(op);
    }

    void visit(const Max *op) {
        visit_binary_op(op);
    }

    void visit(const And *op) {
        visit_binary_op(op);
    }

    void visit(const Or *op) {
        visit_binary_op(op);
    }

    void visit(const Not *op) {
        op->a.accept(this);
        for (size_t i = 0; i < result.size(); i++) {
            result[i] = simplify(Not::make(result[i]));
        }
    }

    void visit(const Select *op) {
        const int width = op->type.width;

        result.resize(width);
        op->condition.accept(this);
        std::vector<Expr> cond = result;

        op->true_value.accept(this);
        std::vector<Expr> true_value = result;

        op->false_value.accept(this);
        std::vector<Expr> false_value = result;

        for (int i = 0; i < width; i++) {
            if (is_const(cond[i])) {
                result[i] = is_one(cond[i]) ? true_value[i] : false_value[i];
            } else {
                result[i] = Select::make(cond[i], true_value[i], false_value[i]);
            }
        }
    }

public:
    std::vector<Expr> result;
    EvaluateSelect() {
    }
};

std::vector<Expr> evaluate_vector_select(const Select *op) {
    EvaluateSelect eval;
    op->accept(&eval);
    return eval.result;
}

void CodeGen_GLSL::visit(const Select *op) {
    string id_value;
    if (op->condition.type().is_scalar()) {
        id_value = unique_name('_');
        do_indent();
        stream << print_type(op->type) << " " << id_value << ";\n";
        string cond = print_expr(op->condition);
        do_indent();
        stream << "if (" << cond << ") ";
        open_scope();
        {
            string true_val = print_expr(op->true_value);
            do_indent();
            stream << id_value << " = " << true_val << ";\n";
        }
        close_scope("");

        do_indent();
        stream << "else ";
        open_scope();
        {
            string false_val = print_expr(op->false_value);
            do_indent();
            stream << id_value << " = " << false_val<< ";\n";
        }
        close_scope("");
    } else {
        // Selects with vector conditions are typically used for constructing
        // vector types. If the select condition can be evaluated at
        // compile-time (which is often the case), we can built the vector
        // directly without lowering to a sequence of "if" statements.
        internal_assert(op->condition.type().width == op->type.width);
        int width = op->type.width;
        std::vector<Expr> result = evaluate_vector_select(op);
        std::vector<std::string> ids(width);
        for (int i = 0; i < width; i++) {
            ids[i] = print_expr(result[i]);
        }
        id_value = unique_name('_');
        do_indent();
        stream << print_type(op->type) << " " << id_value << " = "
               << print_type(op->type) << "(";
        for (int i = 0; i < width; i++) {
            stream << ids[i] << ((i < width - 1) ? ", " : ");\n");
        }
    }

    id = id_value;
}

void CodeGen_GLSL::visit(const Max *op) {
    print_expr(call_builtin(op->type, "max", vec(op->a, op->b)));
}

void CodeGen_GLSL::visit(const Min *op) {
    print_expr(call_builtin(op->type, "min", vec(op->a, op->b)));
}

void CodeGen_GLSL::visit(const Div *op) {
    if (op->type.is_int()) {
        // Halide's integer division is defined to round down. Since the
        // rounding behavior of GLSL's integer division is undefined, emulate
        // the correct behavior using floating point arithmetic.
        Type float_type = Float(32, op->type.width);
        Expr val = Div::make(Cast::make(float_type, op->a), Cast::make(float_type, op->b));
        print_expr(call_builtin(op->type, "floor_f32", vec(val)));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_GLSL::visit(const Mod *op) {
    print_expr(call_builtin(op->type, "mod", vec(op->a, op->b)));
}

std::string CodeGen_GLSL::get_vector_suffix(Expr e) {
    std::vector<Expr> matches;
    Expr w = Variable::make(Int(32), "*");
    // The vectorize pass will insert a ramp in the color dimension argument.
    if (expr_match(Ramp::make(w, 1, 4), e, matches)) {
        // No suffix is needed when accessing a full RGBA vector.
    } else if (expr_match(Ramp::make(w, 1, 3), e, matches)) {
        return ".rgb";
    } else if (expr_match(Ramp::make(w, 1, 2), e, matches)) {
        return ".rg";
    } else if (const IntImm *idx = e.as<IntImm>()) {
        // If the color dimension is not vectorized, e.g. it is unrolled, then
        // then we access each channel individually.
        int i = idx->value;
        internal_assert(0 <= i && i <= 3) <<  "GLSL: color index must be between 0 and 3.\n";
        char suffix[] = "rgba";
        return std::string(".") + suffix[i];
    } else {
        user_error << "GLSL: color index '" << e << "' must be constant.\n"
                   << "Call .bound() or .set_bounds() to specify the range of the color index.\n";
    }
    return "";
}

void CodeGen_GLSL::visit(const Load *) {
    internal_error << "GLSL: unexpected Load node encountered.\n";
}

void CodeGen_GLSL::visit(const Store *) {
    internal_error << "GLSL: unexpected Store node encountered.\n";
}

void CodeGen_GLSL::visit(const Evaluate *op) {
    print_expr(op->value);
}

void CodeGen_GLSL::visit(const Call *op) {
    ostringstream rhs;
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::glsl_texture_load) {
            internal_assert(op->args.size() == 5);

            // Keep track of whether or not the intrinsic was vectorized
            int width = 1;

            // The argument to the call is either a StringImm or a broadcasted
            // StringImm if this is part of a vectorized expression
            internal_assert(op->args[0].as<StringImm>() ||
                            (op->args[0].as<Broadcast>() && op->args[0].as<Broadcast>()->value.as<StringImm>()));

            const StringImm* string_imm = op->args[0].as<StringImm>();
            if (!string_imm) {
                string_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
                width = op->args[0].as<Broadcast>()->width;
            }

            // Determine the halide buffer associated with this load
            string buffername = string_imm->value;

            internal_assert((op->type.code == Type::UInt || op->type.code == Type::Float) &&
                            (op->type.width >= 1 && op->type.width <= 4));

            // In the event that this intrinsic was vectorized, the individual
            // coordinates may be GLSL vecN types instead of scalars. In this case
            // we use only the first element

            rhs << "texture2D(" << print_name(buffername) << ", vec2("
                << print_expr((width > 1) ? op->args[2].as<Broadcast>()->value :  op->args[2]) << ", "
                << print_expr((width > 1) ? op->args[3].as<Broadcast>()->value :  op->args[3]) << "))"
                << get_vector_suffix(op->args[4]);
            if (op->type.is_uint())
                rhs << " * " << op->type.imax() << ".0";

        } else if (op->name == Call::glsl_texture_store) {
            internal_assert(op->args.size() == 6);
            std::string sval = print_expr(op->args[5]);
            do_indent();
            stream << "gl_FragColor" << get_vector_suffix(op->args[4])
                   << " = " << sval;
            if (op->args[5].type().is_uint())
                stream << " / " << op->args[5].type().imax() << ".0";
            stream << ";\n";
            // glsl_texture_store is called only for its side effect; there is
            // no return value.
            id = "";
            return;
        } else if (op->name == Call::lerp) {
            // Implement lerp using GLSL's mix() function, which always uses
            // floating point arithmetic.
            Expr zero_val = op->args[0];
            Expr one_val = op->args[1];
            Expr weight = op->args[2];

            internal_assert(weight.type().is_uint() || weight.type().is_float());
            if (weight.type().is_uint()) {
                // Normalize integer weights to [0.0f, 1.0f] range.
                internal_assert(weight.type().bits < 32);
                weight = Div::make(Cast::make(Float(32), weight),
                                   Cast::make(Float(32), weight.type().imax()));
            } else if (op->type.is_uint()) {
                // Round float weights down to next multiple of (1/op->type.imax())
                // to give same results as lerp based on integer arithmetic.
                internal_assert(op->type.bits < 32);
                weight = floor(weight * op->type.imax()) / op->type.imax();
            }

            Type result_type = Float(32, op->type.width);
            Expr e = call_builtin(result_type, "mix", vec(zero_val, one_val, weight));

            if (!op->type.is_float()) {
                // Mirror rounding implementation of Halide's integer lerp.
                e = Cast::make(op->type, floor(e + 0.5f));
            }
            print_expr(e);

            return;
        } else if (op->name == Call::abs) {
            print_expr(call_builtin(op->type, op->name, op->args));
            return;
        } else if (op->name == Call::return_second) {
            internal_assert(op->args.size() == 2);
            // Simply discard the first argument, which is generally a call to
            // 'halide_printf'.
            rhs << print_expr(op->args[1]);
        } else {
            user_error << "GLSL: intrinsic '" << op->name << "' isn't supported.\n";
            return;
        }
    } else {
        if (builtin.count(op->name) == 0) {
            user_error << "GLSL: unknown function '" << op->name << "' encountered.\n";
        }

        rhs << builtin[op->name] << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) rhs << ", ";
            rhs << print_expr(op->args[i]);
        }
        rhs << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::visit(const AssertStmt *) {
    internal_error << "GLSL: unexpected Assertion node encountered.\n";
}

void CodeGen_GLSL::visit(const Broadcast *op) {
    ostringstream rhs;
    rhs << "vec" << op->width << "(" << print_expr(op->value) << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::compile(Stmt stmt, string name,
                           const vector<GPU_Argument> &args,
                           const Target &target) {
    // Emit special header that declares the kernel name and its arguments.
    // There is currently no standard way of passing information from the code
    // generator to the runtime, and the information Halide passes to the
    // runtime are fairly limited.  We use these special comments to know the
    // data types of arguments and whether textures are used for input or
    // output.
    ostringstream header;
    header << "/// KERNEL " << name << "\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            Type t = args[i].type.element_of();

            user_assert(args[i].read != args[i].write) <<
                "GLSL: buffers may only be read OR written inside a kernel loop.\n";
            std::string type_name;
            if (t == UInt(8)) {
                type_name = "uint8_t";
            } else if (t == UInt(16)) {
                type_name = "uint16_t";
            } else if (t == Float(32)) {
                type_name = "float";
            } else {
                user_error << "GLSL: buffer " << args[i].name << " has invalid type " << t << ".\n";
            }
            header << "/// " << (args[i].read ? "IN_BUFFER " : "OUT_BUFFER ")
                   << type_name << " " << print_name(args[i].name) << "\n";
        } else {
            header << "/// VAR "
                   << CodeGen_C::print_type(args[i].type) << " "
                   << print_name(args[i].name) << "\n";
        }
    }

    stream << header.str();

    // TODO: we need a better way to switch between the different OpenGL
    // versions (desktop GL, GLES2, GLES3, ...), probably by making it part of
    // Target.
    bool opengl_es = (target.os == Target::Android ||
                      target.os == Target::IOS);

    // Specify default float precision when compiling for OpenGL ES.
    // TODO: emit correct #version
    if (opengl_es) {
        stream << "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
               << "precision highp float;\n"
               << "#endif\n";
    }

    // Declare input textures and variables
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer && args[i].read) {
            stream << "uniform sampler2D " << print_name(args[i].name) << ";\n";
        } else if (!args[i].is_buffer) {
            stream << "uniform "
                   << print_type(args[i].type) << " "
                   << print_name(args[i].name) << ";\n";
        }
    }
    // Add pixel position from vertex shader
    stream << "varying vec2 pixcoord;\n";

    stream << "void main() {\n";
    indent += 2;
    print(stmt);
    indent -= 2;
    stream << "}\n";
}

namespace {
// Replace all temporary variables names like _1234 with '$'. This is done to
// make the individual tests below self-contained.
string normalize_temporaries(const string &s) {
    string result;
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '_') {
            result += '$';
            for (i++; i < s.size() && isdigit(s[i]); i++) {
            }
        } else {
            result += s[i++];
        }
    }
    return result;
}

void check(Expr e, const string &result) {
    ostringstream source;
    CodeGen_GLSL cg(source);
    Evaluate::make(e).accept(&cg);
    string src = normalize_temporaries(source.str());
    if (src != result) {
        internal_error
            << "Codegen failed for " << e << "\n"
            << "  Correct source code:\n" << result
            << "  Actual source code:\n" << src;
    }
}
}  // namespace

void CodeGen_GLSL::test() {
    vector<Expr> e;

    // Uint8 is embedded in GLSL floats, so no cast necessary
    check(cast<float>(Variable::make(UInt(8), "x") * 1.0f),
          "float $ = $x * 1.0000000;\n");
    // But truncation is necessary for the reverse direction
    check(cast<uint8_t>(Variable::make(Float(32), "x")),
          "float $ = floor($x);\n");

    check(Min::make(Expr(1), Expr(5)),
          "float $ = min(1.0000000, 5.0000000);\n"
          "int $ = int($);\n");

    check(Max::make(Expr(1), Expr(5)),
          "float $ = max(1.0000000, 5.0000000);\n"
          "int $ = int($);\n");

    check(Max::make(Broadcast::make(1, 4), Broadcast::make(5, 4)),
          "vec4 $ = vec4(1.0000000);\n"
          "vec4 $ = vec4(5.0000000);\n"
          "vec4 $ = max($, $);\n"
          "ivec4 $ = ivec4($);\n");

    check(Variable::make(Int(32), "x") / Expr(3),
          "float $ = float($x);\n"
          "float $ = $ * 0.33333334;\n"
          "float $ = floor($);\n"
          "int $ = int($);\n");
    check(Variable::make(Int(32, 4), "x") / Variable::make(Int(32, 4), "y"),
          "vec4 $ = vec4($x);\n"
          "vec4 $ = vec4($y);\n"
          "vec4 $ = $ / $;\n"
          "vec4 $ = floor($);\n"
          "ivec4 $ = ivec4($);\n");
    check(Variable::make(Float(32, 4), "x") / Variable::make(Float(32, 4), "y"),
          "vec4 $ = $x / $y;\n");

    // Integer lerp with integer weight
    check(lerp(cast<uint8_t>(0), cast<uint8_t>(255), cast<uint8_t>(127)),
          "float $ = mix(0.0000000, 255.00000, 0.49803922);\n"
          "float $ = $ + 0.50000000;\n"
          "float $ = floor($);\n");

    // Integer lerp with float weight
    check(lerp(cast<uint8_t>(0), cast<uint8_t>(255), 0.3f),
          "float $ = mix(0.0000000, 255.00000, 0.29803923);\n"
          "float $ = $ + 0.50000000;\n"
          "float $ = floor($);\n");

    // Floating point lerp
    check(lerp(0.0f, 1.0f, 0.3f),
          "float $ = mix(0.0000000, 1.0000000, 0.30000001);\n");

    // Vectorized lerp
    check(lerp(Variable::make(Float(32, 4), "x"), Variable::make(Float(32, 4), "y"), Broadcast::make(0.25f, 4)),
          "vec4 $ = vec4(0.25000000);\n"
          "vec4 $ = mix($x, $y, $);\n");

    // Sin with scalar arg
    check(sin(3.0f), "float $ = sin(3.0000000);\n");

    // Sin with vector arg
    check(Call::make(Float(32, 4), "sin_f32", vec(Broadcast::make(1.f, 4)), Internal::Call::Extern),
          "vec4 $ = vec4(1.0000000);\n"
          "vec4 $ = sin($);\n");

    // use float version of abs in GLSL
    check(abs(-2),
          "float $ = abs(-2.0000000);\n"
          "int $ = int($);\n");

    check(Halide::print(3.0f), "float $ = 3.0000000;\n");

    // Test rounding behavior of integer division.
    check(Variable::make(Int(32), "x") / Variable::make(Int(32), "y"),
          "float $ = float($x);\n"
          "float $ = float($y);\n"
          "float $ = $ / $;\n"
          "float $ = floor($);\n"
          "int $ = int($);\n");


    check(Select::make(EQ::make(Ramp::make(-1, 1, 4), Broadcast::make(0, 4)),
                       Broadcast::make(1.f, 4),
                       Broadcast::make(2.f, 4)),
          "vec4 $ = vec4(2.0000000, 1.0000000, 2.0000000, 2.0000000);\n");

    check(log(1.0f), "float $ = log(1.0000000);\n");
    check(exp(1.0f), "float $ = exp(1.0000000);\n");

    // Integer powers are expanded
    check(pow(1.4f, 2), "float $ = 1.4000000 * 1.4000000;\n");
    check(pow(1.0f, 2.1f), "float $ = pow(1.0000000, 2.0999999);\n");

    std::cout << "CodeGen_GLSL test passed\n";
}

}}

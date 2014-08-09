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

// Maps Halide types to appropriate GLSL types or emit error if no equivalent
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
        user_assert(type.is_bool() || type.is_int() || type.is_float())
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
// TODO: handle vector types
Expr call_builtin(const Type &result_type,
                  const std::string &func,
                  const std::vector<Expr> &args) {
    std::vector<Expr> new_args(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i].type().is_float()) {
            new_args[i] = Cast::make(Float(32), args[i]);
        } else {
            new_args[i] = args[i];
        }
    }
    Expr val = Call::make(Float(32), func, new_args, Call::Extern);
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
    Type target_type = map_type(op->type);
    Type value_type = op->value.type();
    Expr value = op->value;
    if (target_type == map_type(value_type)) {
        // Skip unneeded casts
        op->value.accept(this);
        return;
    }

    print_assignment(target_type, print_type(target_type) + "(" + print_expr(value) + ")");
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
        user_assert(loop->for_type != For::Parallel) << "Parallel loops aren't allowed inside GLSL\n";
        CodeGen_C::visit(loop);
    }
}

void CodeGen_GLSL::visit(const Select *op) {
    string cond = print_expr(op->condition);

    string id_value = unique_name('_');
    do_indent();
    stream << print_type(op->type) << " " << id_value << ";\n";
    do_indent();
    stream << "if (" << cond << ")\n";
    open_scope(); {
        string true_val = print_expr(op->true_value);
        stream << id_value << " = " << true_val << ";\n";
    } close_scope("");
    do_indent();
    stream << "else\n";
    open_scope(); {
        string false_val = print_expr(op->false_value);
        stream << id_value << " = " << false_val<< ";\n";
        do_indent();
    } close_scope("");

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
        Expr val = Div::make(Cast::make(Float(32), op->a), Cast::make(Float(32), op->b));
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
    if (expr_match(Ramp::make(w, 1, 4), e, matches)) {
        // No suffix is needed when accessing a full RGBA vector.
    } else if (const IntImm *idx = e.as<IntImm>()) {
        int i = idx->value;
        internal_assert(0 <= i && i <= 3) <<  "Color channel must be between 0 and 3.\n";
        char suffix[] = "rgba";
        return std::string(".") + suffix[i];
    } else {
        user_error << "Color index '" << e << "' isn't constant.\n"
                   << "Call .bound() or .set_bounds() to specify the range of the color index.\n";
    }
    return "";
}

void CodeGen_GLSL::visit(const Load *op) {
    internal_error << "Load nodes should have been removed by now\n";
}

void CodeGen_GLSL::visit(const Store *op) {
    internal_error << "Store nodes should have been removed by now\n";
}

void CodeGen_GLSL::visit(const Evaluate *op) {
    print_expr(op->value);
}

void CodeGen_GLSL::visit(const Call *op) {
    ostringstream rhs;
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::glsl_texture_load) {
            internal_assert(op->args.size() == 5);
            internal_assert(op->args[0].as<StringImm>());
            string buffername = op->args[0].as<StringImm>()->value;
            internal_assert(op->type == UInt(8) || op->type == UInt(16));
            rhs << "texture2D(" << print_name(buffername) << ", vec2("
                << print_expr(op->args[2]) << ", "
                << print_expr(op->args[3]) << "))"
                << get_vector_suffix(op->args[4])
                << " * " << op->type.imax() << ".0";
        } else if (op->name == Call::glsl_texture_store) {
            internal_assert(op->args.size() == 6);
            std::string sval = print_expr(op->args[5]);
            internal_assert(op->args[5].type() == UInt(8) || op->args[5].type() == UInt(16));
            do_indent();
            stream << "gl_FragColor" << get_vector_suffix(op->args[4])
                   << " = " << sval << " / " << op->args[5].type().imax() << ".0;\n";

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

            // If weight is an integer, convert it to float and normalize to
            // the [0.0f, 1.0f] range.
            if (weight.type().is_uint()) {
                weight = Div::make(Cast::make(Float(32), weight),
                                   Cast::make(Float(32), weight.type().imax()));
            }

            // If zero_val and one_val are integers, add appropriate type casts.
            print_expr(call_builtin(op->type, "mix", vec(zero_val, one_val, weight)));
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
            user_error << "GLSL: intrinsic '" << op->name << "' isn't supported\n";
            return;
        }
    } else {
        if (builtin.count(op->name) == 0) {
            user_error << "GLSL: encountered unknown function '" << op->name << "'\n";
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
    internal_error << "GLSL: Assertions should not be present\n";
}

void CodeGen_GLSL::visit(const Broadcast *op) {
    ostringstream rhs;
    rhs << "vec4(" << print_expr(op->value) << ")";
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
                "Buffers may only be read OR written inside a kernel loop\n";
            user_assert(t == UInt(8) || t == UInt(16)) <<
                "Buffer " << args[i].name << " has invalid type " << t << ".\n";
            header << "/// " << (args[i].read ? "IN_BUFFER " : "OUT_BUFFER ")
                   << (t == UInt(8) ? "uint8_t " : "uint16_t ")
                   << print_name(args[i].name) << "\n";
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

void CodeGen_GLSL::test() {
    vector<Expr> e;

    e.push_back(Min::make(Expr(1), Expr(5)));
    e.push_back(Max::make(Expr(1), Expr(5)));
    // Lerp with both integer and float weight
    e.push_back(lerp(cast<uint8_t>(0), cast<uint8_t>(255), cast<uint8_t>(127)));
    e.push_back(lerp(cast<uint8_t>(0), cast<uint8_t>(255), 0.3f));
    e.push_back(sin(3.0f));
    e.push_back(abs(-2));
    e.push_back(Halide::print(3.0f));
    e.push_back(-2/Expr(3));  // Test rounding behavior of integer division.

    ostringstream source;
    CodeGen_GLSL cg(source);
    for (size_t i = 0; i < e.size(); i++) {
        Evaluate::make(e[i]).accept(&cg);
    }

    string src = source.str();
    std::string correct_source =
        "float _0 = min(1.0000000, 5.0000000);\n"
        "int _1 = int(_0);\n"
        "float _2 = max(1.0000000, 5.0000000);\n"
        "int _3 = int(_2);\n"
        "float _4 = mix(0.0000000, 255.00000, 0.49803922);\n"
        "float _5 = mix(0.0000000, 255.00000, 0.30000001);\n"
        "float _6 = sin(3.0000000);\n"
        "float _7 = abs(-2.0000000);\n"
        "int _8 = int(_7);\n"
        "float _9 = 3.0000000;\n"
        "float _10 = floor(-0.66666669);\n"
        "int _11 = int(_10);\n";

    if (src != correct_source) {
        int diff = 0;
        while (src[diff] == correct_source[diff]) diff++;
        int diff_end = diff + 1;
        while (diff > 0 && src[diff] != '\n') diff--;
        while (diff_end < (int)src.size() && src[diff_end] != '\n') diff_end++;

        internal_error
            << "Correct source code:\n" << correct_source
            << "Actual source code:\n" << src
            << "\nDifference starts at: " << src.substr(diff, diff_end - diff) << "\n";

    }

    std::cout << "CodeGen_GLSL test passed\n";
}


}}

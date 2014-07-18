#include "CodeGen_OpenGL_Dev.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "Debug.h"
#include "Simplify.h"
#include <iomanip>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

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
static Type map_type(const Type &type) {
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

}

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev(const Target &target)
    : target(target) {
    debug(1) << "Creating GLSL codegen\n";
    glc = new CodeGen_GLSL(src_stream);
}

CodeGen_OpenGL_Dev::~CodeGen_OpenGL_Dev() {
    delete glc;
}

void CodeGen_OpenGL_Dev::add_kernel(Stmt s, string name,
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
    } else if (target_type == Float(32)) {
        // Casts to float are common in GLSL; simplify them if possible
        if (const IntImm *imm = op->value.as<IntImm>()) {
            value = FloatImm::make(static_cast<float>(imm->value));
            value.accept(this);
            return;
        }
    }

    // Casts expressions are simple enough that we return them directly
    // instead of calling print_assignment.
    id = print_type(target_type) + "(" + print_expr(value) + ")";
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
    // version 120 only supports min of floats, so we have to cast back and forth
    Expr a = op->a;
    if (!(op->a.type().is_float())){
        a = Cast::make(Float(a.type().bits), a);
    }
    Expr b = op->b;
    if (!b.type().is_float()){
        b = Cast::make(Float(b.type().bits), b);
    }
    Expr out = Call::make(Float(32), "max", vec(a, b), Call::Extern);
    if (!op->type.is_float()) {
        print_expr(Cast::make(op->type, out));
    } else {
        print_expr(out);
    }
}

void CodeGen_GLSL::visit(const Min *op) {
    // version 120 only supports min of floats, so we have to cast back and forth
    Expr a = op->a;
    if (!(op->a.type().is_float())){
        a = Cast::make(Float(a.type().bits), a);
    }
    Expr b = op->b;
    if (!b.type().is_float()){
        b = Cast::make(Float(b.type().bits), b);
    }
    Expr out = Call::make(Float(32), "min", vec(a, b), Call::Extern);
    if (!op->type.is_float()) {
        print_expr(Cast::make(op->type, out));
    } else {
        print_expr(out);
    }
}

void CodeGen_GLSL::visit(const Div *op) {
    if (op->type.is_int()) {
        print_expr(Call::make(op->type, "sdiv", vec(op->a, op->b), Call::Extern));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_GLSL::visit(const Mod *op) {
    print_expr(Call::make(op->type, "mod", vec(op->a, op->b), Call::Extern));
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
        internal_error << "Color index '" << e << "' isn't constant.\n"
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
    op->value.accept(this);
}

void CodeGen_GLSL::visit(const Call *op) {
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::glsl_texture_load) {
            internal_assert(op->args.size() == 5);
            internal_assert(op->args[0].as<StringImm>());
            string buffername = op->args[0].as<StringImm>()->value;
            ostringstream rhs;
            internal_assert(op->type == UInt(8) || op->type == UInt(16));
            rhs << "texture2D(" << print_name(buffername) << ", vec2("
                << print_expr(op->args[2]) << ", "
                << print_expr(op->args[3]) << "))"
                << get_vector_suffix(op->args[4])
                << " * " << op->type.imax() << ".0";
            print_assignment(op->type, rhs.str());
        } else if (op->name == Call::glsl_texture_store) {
            internal_assert(op->args.size() == 6);
            std::string sval = print_expr(op->args[5]);
            internal_assert(op->args[5].type() == UInt(8) || op->args[5].type() == UInt(16));
            do_indent();
            stream << "gl_FragColor" << get_vector_suffix(op->args[4])
                   << " = " << sval << " / " << op->args[5].type().imax() << ".0;\n";
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
            // Lerp guarantees that op->type == zero_val.type() == one_val.type().
            if (zero_val.type().is_float()) {
                print_expr(Call::make(op->type, "mix",
                                      vec(zero_val, one_val, weight),
                                      Call::Extern));
            } else {
                zero_val = Cast::make(Float(32), zero_val);
                one_val = Cast::make(Float(32), one_val);
                print_expr(Cast::make(op->type,
                                      Call::make(Float(32), "mix",
                                                 vec(zero_val, one_val, weight),
                                                 Call::Extern)));
            }
        } else {
            CodeGen_C::visit(op);
        }
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_GLSL::visit(const AssertStmt *) {
    internal_error << "Assertions should not be present in GLSL\n";
}

void CodeGen_GLSL::visit(const Broadcast *op) {
    ostringstream rhs;
    rhs << "vec4(" << print_expr(op->value) << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::compile(Stmt stmt,
                           string name,
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
                   << (t == UInt(8) ? "uint8 " : "uint16 ")
                   << print_name(args[i].name) << "\n";
        } else {
            header << "/// VAR "
                   << print_type(args[i].type) << " "
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
        stream << "precision highp float;\n";
    }
    stream << "#define sdiv(a, b) ((a - (int)mod((float)a, (float)b))/b)\n"
           << "#define sqrt_f32 sqrt\n"
           << "#define sin_f32 sin\n"
           << "#define cos_f32 cos\n"
           << "#define exp_f32 exp\n"
           << "#define log_f32 log\n"
           << "#define abs_f32 abs\n"
           << "#define floor_f32 floor\n"
           << "#define ceil_f32 ceil\n"
           << "#define pow_f32 pow\n"
           << "#define asin_f32 asin\n"
           << "#define acos_f32 acos\n"
           << "#define tan_f32 tan\n"
           << "#define atan_f32 atan\n"
           << "#define atan2_f32 atan\n";


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

}}

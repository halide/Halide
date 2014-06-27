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
}

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev(const Target &target)
    : target(target) {
    debug(1) << "Creating GLSL codegen\n";
    glc = new CodeGen_GLSL(src_stream);
}

CodeGen_OpenGL_Dev::~CodeGen_OpenGL_Dev() {
    delete glc;
}

void CodeGen_OpenGL_Dev::add_kernel(Stmt s,
                                    string name,
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
    if (type.is_scalar()) {
        if (type.is_float()) {
            if (type.bits == 32) {
                oss << "float";
            } else {
                user_error << "Can't represent a float with " << type.bits << " bits in GLSL\n";
            }
        } else if (type.bits == 1) {
            oss << "bool";
        } else if (type == Int(32)) {
            oss << "int";
        } else if (type.bits <= 16) {
            // Embed all other ints in a glsl float. Probably not
            // actually valid for uint16 on systems with low float
            // precision.
            oss << "float";
        } else {
            user_error << "Can't represent type '"<< type << "' in GLSL\n";
        }
    } else if (type.width <= 4) {
        if (type.is_bool()) {
            oss << "b";
        } else if (type.is_int()) {
            oss << "i";
        } else if (type.is_float()) {
            // no prefix for float vectors
        } else {
            user_error << "Can't represent type '"<< type << "' in GLSL\n";
        }
        oss << "vec" << type.width;
    } else {
        user_error << "Vector types wider than 4 aren't supported in GLSL\n";
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
    print_assignment(op->type,
                     print_type(op->type) + "(" + print_expr(op->value) + ")");
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
        internal_error << "Color index '" << e << "' not supported\n";
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
    if (op->call_type == Call::Intrinsic && op->name == Call::glsl_texture_load) {
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
        return;
    } else if (op->call_type == Call::Intrinsic && op->name == Call::glsl_texture_store) {
        internal_assert(op->args.size() == 6);
        std::string sval = print_expr(op->args[5]);
        internal_assert(op->args[5].type() == UInt(8) || op->args[5].type() == UInt(16));
        do_indent();
        stream << "gl_FragColor" << get_vector_suffix(op->args[4])
               << " = " << sval << " / " << op->args[5].type().imax() << ".0;\n";
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
    stream << "#define sin_f32 sin\n";
    stream << "#define cos_f32 cos\n";
    stream << "#define sqrt_f32 sqrt\n";

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

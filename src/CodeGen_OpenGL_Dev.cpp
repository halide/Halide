#include "CodeGen_OpenGL_Dev.h"
#include "IRMatch.h"
#include "Debug.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

static ostringstream nil;

CodeGen_OpenGL_Dev::CodeGen_OpenGL_Dev() {
    debug(1) << "Creating GLSL codegen\n";
    glc = new CodeGen_GLSL(src_stream);
}

CodeGen_OpenGL_Dev::~CodeGen_OpenGL_Dev() {
    delete glc;
}

void CodeGen_OpenGL_Dev::add_kernel(Stmt s, string name,
                                    const vector<Argument> &args) {
    cur_kernel_name = name;
    glc->compile(s, name, args);
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
                assert(false && "Can't represent a float with this many bits in GLSL");
            }
        } else if (type.bits == 1) {
            oss << "bool";
        } else if (type.is_int()) {
            if (type.bits == 32) {
                oss << "int";
            } else {
                assert("Can't represent an integer with this many bits in GLSL");
            }
        } else {
            assert(false && "Can't represent this type in GLSL");
        }
    } else if (type.width <= 4) {
        if (type.is_bool()) {
            oss << "b";
        } else if (type.is_int()) {
            oss << "i";
        } else if (type.is_float()) {
            // no prefix for float vectors
        } else {
            assert(false && "Can't represent this type in GLSL");
        }
        oss << "vec" << type.width;
    } else {
        assert(false && "Vector types wider than 4 aren't supported in GLSL");
    }
    return oss.str();
}

void CodeGen_GLSL::visit(const FloatImm *op) {
    // TODO(dheck): use something like dtoa to avoid precision loss in
    // float->decimal conversion
    ostringstream oss;
    oss << op->value;
    id = oss.str();
}

void CodeGen_GLSL::visit(const Cast *op) {
    const Load *load = op->value.as<Load>();
    if (op->type.is_float() && load) {
        // texture2D performs implicit conversion from the internal texture
        // format to float, so drop the explicit cast.
        Expr new_load = Load::make(Float(32, load->type.width), load->name, load->index, load->image, load->param);
        new_load.accept(this);
    } else {
        print_assignment(op->type,
                         print_type(op->type) + "(" + print_expr(op->value) + ")");
    }
}

void CodeGen_GLSL::visit(const For *loop) {
    if (ends_with(loop->name, ".blockidx") || ends_with(loop->name, ".blockidy")) {
        debug(1) << "Dropping loop " << loop->name << " (" << loop->min << ", " << loop->extent << ")\n";
        assert(loop->for_type == For::Kernel && "Expecting kernel loop");

        string idx;
        if (ends_with(loop->name, ".blockidx")) {
            idx = "pixcoord.x";
        } else if (ends_with(loop->name, ".blockidy")) {
            idx = "pixcoord.y";
        }
        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name) << " = " << idx << ";\n";
        loop->body.accept(this);
    } else {
    	assert(loop->for_type != For::Parallel && "Cannot emit parallel loops in OpenGL C");
    	CodeGen_C::visit(loop);
    }
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
        assert(0 <= i && i <= 3 && "Color channel must be between 0 and 3.");
        char suffix[] = "rgba";
        return std::string(".") + suffix[i];
    } else {
        debug(0) << "Color index: " << e << "\n";
        assert(false && "Color index not supported");
    }
    return "";
}


void CodeGen_GLSL::visit(const Load *op) {
    assert(op->index.size() == 3 && "Load from texture requires multi-index");

    ostringstream rhs;
    rhs << "texture2D(" << op->name
        << ", ivec2("
        << print_expr(op->index[0]) << ", "
        << print_expr(op->index[1]) << "))"
        << get_vector_suffix(op->index[2]);

    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::visit(const Store *op) {
    assert(op->index.size() == 3 && "Store to texture requires multi-index");
    std::vector<Expr> matches;

    const Cast *cast = op->value.as<Cast>();
    if (cast && cast->type.element_of() == UInt(8)) {
        // Writing to gl_FragColor performs implicit conversion from float to
        // the internal texture format to float, so drop the explicit cast.
        Stmt new_store = Store::make(op->name, cast->value, op->index);
        new_store.accept(this);
    } else {
        string val = print_expr(op->value);
        do_indent();
        stream << "gl_FragColor" << get_vector_suffix(op->index[2]) << " = "
               << val
               << ";\n";
    }
}

void CodeGen_GLSL::visit(const Call *op) {
    CodeGen_C::visit(op);
}

void CodeGen_GLSL::visit(const AssertStmt *) {
    debug(1) << "GLSL doesn't support assertions... dropped\n";
}

void CodeGen_GLSL::visit(const Broadcast *op) {
    ostringstream rhs;
    rhs << "vec4(" << print_expr(op->value) << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_GLSL::compile(Stmt stmt, string name,
                           const vector<Argument> &args) {

    // Emit special header that declares the kernel name and its arguments.
    // There is currently no standard way of passing information from the code
    // generator to the runtime, and the information Halide passes to the
    // runtime are fairly limited.  We use these special comments to know the
    // data types of arguments and whether textures are used for input or
    // output.
    ostringstream header;
    header << "/// KERNEL " << print_name(name) << "\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            Type t = args[i].type.element_of();

            assert(args[i].read != args[i].write &&
                   "Buffers may only be read OR written inside a kernel loop");
            assert((t == UInt(8) || t == UInt(16)) &&
                   "Only uint8 and uint16 buffers are supported by OpenGL");
            header << "/// " << (args[i].read ? "IN_BUFFER " : "OUT_BUFFER ")
                   << (t == UInt(8) ? "uint8 " : "uint16 ")
                   << print_name(args[i].name) << "\n";
        } else {
            header << "/// VAR "
                   << print_type(args[i].type) << " "
                   << print_name(args[i].name) << "\n";
        }
    }

    stream << "#version 120\n";
    stream << header.str();

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer && args[i].read) {
            // Declare input textures
            stream << "uniform sampler2D " << print_name(args[i].name) << ";\n";
        } else if (!args[i].is_buffer) {
            // Declare input variables
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

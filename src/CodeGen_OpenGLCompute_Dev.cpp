#include "CodeGen_OpenGLCompute_Dev.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "Simplify.h"
#include "VaryingAttributes.h"
#include <iomanip>
#include <map>
#include <limits>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;
using std::map;


CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_Dev(Target target)
    : glc(src_stream), target(target) {
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
        } else if (type == Int(32) || type == UInt(32)) {
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
        user_assert(type.is_bool() || type.is_int() || type.is_uint() || type.is_float())
            << "GLSL: Can't represent vector type '"<< type << "'.\n";
        Type scalar_type = type;
        scalar_type.width = 1;
        result = map_type(scalar_type);
        result.width = type.width;
    }
    return result;
}

string CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::print_type(Type type) {
    ostringstream oss;
    type = map_type(type);
    if (type.is_scalar()) {
        if (type.is_float()) {
            oss << "float";
        } else if (type.is_bool()) {
            oss << "bool";
        } else if (type.is_int()) {
            oss << "int";
        } else if (type.is_uint()) {
            oss << "uint";
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

// string CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::print_reinterpret(Type type, Expr e) {
//     ostringstream oss;
//     oss << "as_" << print_type(type) << "(" << print_expr(e) << ")";
//     return oss.str();
// }

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "gl_LocalInvocationID.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "gl_LocalInvocationID.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "gl_LocalInvocationID.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        internal_error << "4-dimension loops with " << name << " are not supported\n";
    } else if (ends_with(name, ".__block_id_x")) {
        return "gl_WorkGroupID.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "gl_WorkGroupID.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "gl_WorkGroupID.z";
    } else if (ends_with(name, ".__block_id_w")) {
        internal_error << "4-dimension loops with " << name << " are not supported\n";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Div *op) {
    visit_binop(op->type, op->a, op->b, "/");
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Mod *op) {
    visit_binop(op->type, op->a, op->b, "%");
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        internal_assert(loop->for_type == ForType::Parallel) << "kernel loop must be parallel\n";
        internal_assert(is_zero(loop->min));

        do_indent();
        stream << print_type(Int(32)) << " " << print_name(loop->name)
               << " = int(" << simt_intrinsic(loop->name) << ");\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside OpenGLCompute kernel\n";
        CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Ramp *op) {
    ostringstream rhs;
    rhs << print_type(op->type) << "(";

    if (op->width > 4)
        internal_error << "GLSL: ramp width " << op->width << " is not supported\n";

    rhs << print_expr(op->base);

    for (int i = 1; i < op->width; ++i) {
        rhs << ", " << print_expr(Add::make(op->base, Mul::make(i, op->stride)));
    }

    rhs << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);
    ostringstream oss;
    oss << print_type(op->type.vector_of(op->width)) << "(" << id_value << ")";
    print_assignment(op->type.vector_of(op->width), oss.str());
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Load *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    if (!ramp) {
        user_error << "Only vectorized images are supported by OpenGLCompute.\n";
        return;
    }
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
    user_assert(stride && stride->value == 1 && ramp->width == 4) <<
        "Only trivial packed 4x vectors(stride==1, width==4) are supported by OpenGLCompute.";

    ostringstream oss;
    oss << "imageLoad(" << print_name(op->name) << ", " << print_expr(ramp->base) << ")";
    print_assignment(op->type, oss.str());
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Store *op) {
    const Ramp *ramp = op->index.as<Ramp>();
    if (!ramp) {
        user_error << "Only vectorized images are supported by OpenGLCompute.\n";
        return;
    }
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
    user_assert(stride && stride->value == 1 && ramp->width == 4) <<
        "Only trivial packed 4x vectors(stride==1, width==4) are supported by OpenGLCompute.";

    string id_index = print_expr(ramp->base);
    string id_value = print_expr(op->value);

    do_indent();
    stream << "imageStore(" << print_name(op->name) << ", " << id_index << ", " << id_value << ");\n";
}

void CodeGen_OpenGLCompute_Dev::add_kernel(Stmt s,
                                           const string &name,
                                           const vector<GPU_Argument> &args) {
    debug(2) << "CodeGen_OpenGLCompute_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    glc.add_kernel(s, name, args);
}

namespace {
string print_image_buffer_type(Type type) {
    if (type.is_float()) {
        switch(type.width) {
            case 1: return "r32f";
            case 4: return "rgba32f";
        }
    }
    user_error << "OpenGLCompute: Images of type " << type << " are not supported.\n";
    return "";
}
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::add_kernel(Stmt s,
                                                      const string &name,
                                                      const vector<GPU_Argument> &args) {

    debug(2) << "Adding OpenGLCompute kernel " << name << "\n";

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            stream << "layout(binding=" << i << ", " << print_image_buffer_type(args[i].type) << ")"
                << " uniform mediump " << (args[i].write? "writeonly": "readonly")
                << " imageBuffer " << print_name(args[i].name) << ";\n";
        } else {
            stream << "uniform " << print_type(args[i].type) << " " << print_name(args[i].name) << ";\n";
        }
    }

    stream << R"EOF(
void main()
{
)EOF";
    indent += 2;
    print(s);
    indent -= 2;
    stream << "}\n";
}

void CodeGen_OpenGLCompute_Dev::init_module() {
    src_stream.str("");
    src_stream.clear();

    const int workgroupSize = 128;

    src_stream << R"(#version 310 es
#define LOCAL_SIZE )" << workgroupSize << R"EOF(
#extension GL_ANDROID_extension_pack_es31a : require

layout(local_size_x = LOCAL_SIZE) in;

float float_from_bits(int x) { return intBitsToFloat(x); }
)EOF";

    cur_kernel_name = "";
}

vector<char> CodeGen_OpenGLCompute_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "GLSL source:\n" << str << '\n';
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenGLCompute_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenGLCompute_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

std::string CodeGen_OpenGLCompute_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}}

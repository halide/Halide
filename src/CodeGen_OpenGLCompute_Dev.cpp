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
        } else if (type.is_uint()) {
            oss << "u";
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

int thread_loop_workgroup_index(const string &name) {
    string ids[] = {".__thread_id_x",
                    ".__thread_id_y",
                    ".__thread_id_z",
                    ".__thread_id_w"};
     for (size_t i = 0; i < sizeof(ids) / sizeof(string); i++) {
        if (ends_with(name, ids[i])) { return i; }
     }
     return -1;
}
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Div *op) {
    visit_binop(op->type, op->a, op->b, "/");
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Mod *op) {
    visit_binop(op->type, op->a, op->b, "%");
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Cast *op) {
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

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const For *loop) {
    if (is_gpu_var(loop->name)) {
        internal_assert(loop->for_type == ForType::Parallel) << "kernel loop must be parallel\n";
        internal_assert(is_zero(loop->min));

        debug(4) << "loop extent is " << loop->extent << "\n";
        //
        //  Need to extract workgroup size.
        //
        int index = thread_loop_workgroup_index(loop->name);
        if (index >= 0) {
            const IntImm *int_limit = loop->extent.as<IntImm>();
            user_assert(int_limit != NULL) << "For OpenGLCompute workgroup size must be an constant integer.\n";
            int new_workgroup_size = int_limit->value;
            user_assert(workgroup_size[index] == 0 || workgroup_size[index] == new_workgroup_size) <<
                "OpenGLCompute requires all gpu kernels have same workgroup size, "
                "but two different ones were encountered " << workgroup_size << " and " << new_workgroup_size << ".\n";
            workgroup_size[index] = new_workgroup_size;
            debug(4) << "Workgroup size for index " << index << " is " << workgroup_size[index] << "\n";
        }

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
    print_assignment(op->base.type(), rhs.str());
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);
    ostringstream oss;
    oss << print_type(op->type.vector_of(op->width)) << "(" << id_value << ")";
    print_assignment(op->type.vector_of(op->width), oss.str());
}

// namespace {

// Expr strip_mul_by_4(Expr base) {
//     const Mul *mul = base.as<Mul>();
//     user_assert(mul != NULL) << "OpenGLCompute expects multipication for the index. Got " << base << "instead.\n";
//     const IntImm *factor = mul->b.as<IntImm>();
//     Expr index = mul->a;
//     if (factor == NULL) {
//         factor = mul->a.as<IntImm>();
//         index = mul->b;
//     }
//     user_assert(factor != NULL && factor->value == 4) <<
//         "Only fully vectorized access is supported by OpenGLCompute(base is not multiple of 4)";

//     return index;
// }
// }

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Load *op) {
    string id_index;
    const Ramp *ramp = op->index.as<Ramp>();
    if (ramp) {
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
        user_assert(stride && stride->value == 1 && ramp->width == 4) <<
            "Only trivial packed 4x vectors(stride==1, width==4) are supported by OpenGLCompute.";

        // Expr quarter_index = strip_mul_by_4(ramp->base);
        // id_index = print_expr(quarter_index);
        id_index = print_expr(Div::make(ramp->base, IntImm::make(4)));
    } else {
        id_index = print_expr(op->index);
    }

    ostringstream oss;
    oss << print_name(op->name) << ".data[" << id_index << "]";
    print_assignment(op->type, oss.str());
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Store *op) {
    string id_index;
    const Ramp *ramp = op->index.as<Ramp>();
    if (ramp) {
        const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
        user_assert(stride && stride->value == 1 && ramp->width == 4) <<
            "Only trivial packed 4x vectors(stride==1, width==4) are supported by OpenGLCompute.";

        // Expr quarter_index = strip_mul_by_4(ramp->base);
        // id_index = print_expr(quarter_index);
        id_index = print_expr(Div::make(ramp->base, IntImm::make(4)));
    } else {
        id_index = print_expr(op->index);
    }

    string id_value = print_expr(op->value);

    do_indent();
    stream << print_name(op->name) << ".data[" << id_index << "] = " << id_value << ";\n";
}

void CodeGen_OpenGLCompute_Dev::add_kernel(Stmt s,
                                           const string &name,
                                           const vector<GPU_Argument> &args) {
    debug(2) << "CodeGen_OpenGLCompute_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    glc.add_kernel(s, name, args);
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::add_kernel(Stmt s,
                                                      const string &name,
                                                      const vector<GPU_Argument> &args) {

    debug(2) << "Adding OpenGLCompute kernel " << name << "\n";

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            //
            // layout(binding = 10) buffer buffer10 {
            //     vec3 data[];
            // } inBuffer;
            //
            stream << "layout(binding=" << i << ")"
                << " buffer buffer" << i << " { "
                << print_type(args[i].type) << " data[]; } " << print_name(args[i].name) << ";\n";
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

    indent += 2;
    stream << "layout(local_size_x = " << workgroup_size[0];
    if (workgroup_size[1] > 1) { stream << ", local_size_y = " << workgroup_size[1]; }
    if (workgroup_size[2] > 1) { stream << ", local_size_z = " << workgroup_size[2]; }
    stream << ") in;\n";
}

void CodeGen_OpenGLCompute_Dev::init_module() {
    src_stream.str("");
    src_stream.clear();

    src_stream << R"EOF(#version 310 es
#extension GL_ANDROID_extension_pack_es31a : require

float float_from_bits(int x) { return intBitsToFloat(x); }
)EOF";

    cur_kernel_name = "";
    glc.workgroup_size[0] = 0;
    glc.workgroup_size[1] = 0;
    glc.workgroup_size[2] = 0;
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Allocate *op) {
    debug(2) << "OpenGLCompute: Allocate " << op->name << " of type " << op->type << " on device\n";

    do_indent();
    stream << "// Got allocation " << op->name << ".\n";
    allocations.push(op->name, op->type);

    op->body.accept(this);
}

void CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_C::visit(const Free *op) {
    debug(2) << "OpenGLCompute: Free on device for " << op->name << "\n";

    allocations.pop(op->name);
    do_indent();
    stream << "// Lost allocation " << (op->name) << "\n";
}

vector<char> CodeGen_OpenGLCompute_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "GLSL Compute source:\n" << str << '\n';
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

#include "CodeGen_OpenGLCompute_Dev.h"
#include "CSE.h"
#include "CodeGen_C.h"
#include "CodeGen_GPU_Dev.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include <iomanip>
#include <limits>
#include <map>

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::string;
using std::vector;

namespace {

char get_lane_suffix(int i) {
    internal_assert(i >= 0 && i < 4);
    return "rgba"[i];
}

class CodeGen_OpenGLCompute_C : public CodeGen_C {
public:
    CodeGen_OpenGLCompute_C(std::ostream &s, const Target &t);
    void add_kernel(const Stmt &stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args);

protected:
    Type map_type(const Type &);

    std::string print_name(const std::string &name) override;
    std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;

    using CodeGen_C::visit;

    void visit(const Cast *) override;

    void visit(const FloatImm *) override;
    void visit(const UIntImm *) override;
    void visit(const IntImm *) override;

    void visit(const Max *op) override;
    void visit(const Min *op) override;

    void visit(const Mod *) override;

    // these have specific functions
    // in GLSL that operate on vectors
    void visit(const EQ *) override;
    void visit(const NE *) override;
    void visit(const LT *) override;
    void visit(const LE *) override;
    void visit(const GT *) override;
    void visit(const GE *) override;

    void visit(const Shuffle *) override;

    void visit(const For *) override;
    void visit(const Ramp *op) override;
    void visit(const Broadcast *op) override;
    void visit(const Load *op) override;
    void visit(const Store *op) override;
    void visit(const Call *op) override;
    void visit(const Allocate *op) override;
    void visit(const Free *op) override;
    void visit(const Select *op) override;
    void visit(const Evaluate *op) override;

    const std::map<std::string, std::string> builtin = {
        {"abs", "abs"},
        {"abs_f32", "abs"},
        {"acos_f32", "acos"},
        {"acosh_f32", "acosh"},
        {"asin_f32", "asin"},
        {"asinh_f32", "asinh"},
        {"atan2_f32", "atan"},  // also called atan in GLSL
        {"atan_f32", "atan"},
        {"atanh_f32", "atanh"},
        {"ceil_f32", "ceil"},
        {"cos_f32", "cos"},
        {"cosh_f32", "cosh"},
        {"equal", "equal"},
        {"exp_f32", "exp"},
        {"fast_inverse_sqrt_f32", "inversesqrt"},
        {"floor_f32", "floor"},
        {"greaterThan", "greaterThan"},
        {"greaterThanEqual", "greaterThanEqual"},
        {"isnan", "isnan"},
        {"lessThan", "lessThan"},
        {"lessThanEqual", "lessThanEqual"},
        {"log_f32", "log"},
        {"max", "max"},
        {"min", "min"},
        {"mix", "mix"},
        {"mod", "mod"},
        {"notEqual", "notEqual"},
        {"round_f32", "roundEven"},
        {"sin_f32", "sin"},
        {"sinh_f32", "sinh"},
        {"sqrt_f32", "sqrt"},
        {"tan_f32", "tan"},
        {"tanh_f32", "tanh"},
        {"trunc_f32", "trunc"},
    };
    int workgroup_size[3] = {0, 0, 0};

    // Maps each buffer with whether its base type is a vector.
    std::map<string, bool> buffer_is_vector;
};

CodeGen_OpenGLCompute_C::CodeGen_OpenGLCompute_C(std::ostream &s, const Target &t)
    : CodeGen_C(s, t) {
}

// Maps Halide types to appropriate GLSL types or emit error if no equivalent
// type is available.
Type CodeGen_OpenGLCompute_C::map_type(const Type &type) {
    Type result = type;
    if (type.is_scalar()) {
        if (type.is_float()) {
            user_assert(type.bits() <= 32)
                << "GLSL: Can't represent a float with " << type.bits() << " bits.\n";
            result = Float(32);
        } else if (type.is_bool()) {
            // unchanged
        } else if (type.is_int() && type.bits() <= 32) {
            result = Int(32);
        } else if (type.is_uint() && type.bits() <= 32) {
            result = UInt(32);
        } else {
            user_error << "GLSL: Can't represent type '" << type << "'.\n";
        }
    } else {
        user_assert(type.lanes() <= 4)
            << "GLSL: vector types wider than 4 aren't supported\n";
        user_assert(type.is_bool() || type.is_int() || type.is_uint() || type.is_float())
            << "GLSL: Can't represent vector type '" << type << "'.\n";
        Type scalar_type = type.element_of();
        result = map_type(scalar_type).with_lanes(type.lanes());
    }
    return result;
}

// Identifiers containing double underscores '__' are reserved in GLSL, so we
// have to use a different name mangling scheme than in the C code generator.
string CodeGen_OpenGLCompute_C::print_name(const string &name) {
    const string mangled = CodeGen_C::print_name(name);
    return replace_all(mangled, "__", "XX");
}

string CodeGen_OpenGLCompute_C::print_type(Type type, AppendSpaceIfNeeded space) {
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
        oss << "vec" << type.lanes();
    }

    if (space == AppendSpace) {
        oss << " ";
    }

    return oss.str();
}

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
        if (ends_with(name, ids[i])) {
            return i;
        }
    }
    return -1;
}

void CodeGen_OpenGLCompute_C::visit(const FloatImm *op) {
    ostringstream oss;
    // Print integral numbers with trailing ".0". For fractional numbers use a
    // precision of 9 digits, which should be enough to recover the binary
    // float unambiguously from the decimal representation (if iostreams
    // implements correct rounding).
    const float truncated = (op->value < 0 ? std::ceil(op->value) : std::floor(op->value));
    if (truncated == op->value) {
        oss << std::fixed << std::setprecision(1) << op->value;
    } else {
        oss << std::setprecision(9) << op->value;
    }
    id = oss.str();
}

void CodeGen_OpenGLCompute_C::visit(const UIntImm *op) {
    if (op->type == Bool()) {
        if (op->value == 1) {
            id = "true";
        } else {
            id = "false";
        }
    } else {
        id = std::to_string(op->value) + "u";
    }
}

void CodeGen_OpenGLCompute_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::PureExtern));
}

void CodeGen_OpenGLCompute_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::PureExtern));
}

void CodeGen_OpenGLCompute_C::visit(const Mod *op) {
    if (op->type.is_int() || op->type.is_uint()) {
        // Just exploit the Euclidean identity
        // FIXME: Why doesn't lower_euclidean_mod work for glsl?
        // https://github.com/halide/Halide/issues/4979
        Expr zero = make_zero(op->type);
        Expr equiv = select(op->a == zero, zero,
                            op->a - (op->a / op->b) * op->b);
        equiv = common_subexpression_elimination(equiv);
        print_expr(equiv);
    } else {
        print_expr(Call::make(op->type, "mod", {op->a, op->b}, Call::Extern));
    }
}

// The following comparisons are defined for ivec and vec
// types, so we don't use call_builtin
void CodeGen_OpenGLCompute_C::visit(const EQ *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "equal", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const NE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "notEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const LT *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "lessThan", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const LE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "lessThanEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const GT *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "greaterThan", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const GE *op) {
    if (op->type.is_vector()) {
        print_expr(Call::make(op->type, "greaterThanEqual", {op->a, op->b}, Call::Extern));
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_OpenGLCompute_C::visit(const Shuffle *op) {
    // The halide Shuffle represents the llvm intrinisc
    // shufflevector, however, for GLSL its use is limited to swizzling
    // up to a four channel vec type.

    internal_assert(op->vectors.size() == 1);

    int shuffle_lanes = op->type.lanes();
    internal_assert(shuffle_lanes <= 4);

    string expr = print_expr(op->vectors[0]);

    // Create a swizzle expression for the shuffle
    string swizzle;
    for (int i = 0; i != shuffle_lanes; ++i) {
        int channel = op->indices[i];
        internal_assert(channel < 4) << "Shuffle of invalid channel";
        swizzle += get_lane_suffix(channel);
    }

    print_assignment(op->type, expr + "." + swizzle);
}

void CodeGen_OpenGLCompute_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        const auto *fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        stream << get_indent() << "barrier();\n";

        // barrier() is an execution barrier; for memory behavior, we'll use the
        // least-common-denominator groupMemoryBarrier(), because other fence types
        // require extensions or GL 4.3 as a minumum.
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device ||
            fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared) {
            stream << "groupMemoryBarrier();\n";
        }
        print_assignment(op->type, "0");
    } else if (op->is_intrinsic(Call::lerp)) {
        // Implement lerp using GLSL's mix() function, which always uses
        // floating point arithmetic.
        Expr zero_val = op->args[0];
        Expr one_val = op->args[1];
        Expr weight = op->args[2];

        internal_assert(weight.type().is_uint() || weight.type().is_float());
        if (weight.type().is_uint()) {
            // Normalize integer weights to [0.0f, 1.0f] range.
            internal_assert(weight.type().bits() < 32);
            weight = Div::make(Cast::make(Float(32), weight),
                               Cast::make(Float(32), weight.type().max()));
        } else if (op->type.is_uint()) {
            // Round float weights down to next multiple of (1/op->type.imax())
            // to give same results as lerp based on integer arithmetic.
            internal_assert(op->type.bits() < 32);
            weight = floor(weight * op->type.max()) / op->type.max();
        }

        Type result_type = Float(32, op->type.lanes());
        Expr e = Call::make(result_type, "mix", {zero_val, one_val, weight}, Call::Extern);

        if (!op->type.is_float()) {
            // Mirror rounding implementation of Halide's integer lerp.
            e = Cast::make(op->type, floor(e + 0.5f));
        }
        print_expr(e);
        return;
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        Expr a = op->args[0];
        Type target_type = map_type(op->type);
        if (op->type != Int(32)) {
            print_assignment(target_type, print_type(target_type) + "(abs(" + print_expr(a) + "))");
        } else {
            print_assignment(target_type, "abs(" + print_expr(a) + ")");
        }
        return;
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = cast(op->type, select(a < b, b - a, a - b));
        print_expr(e);
        return;
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        // Simply discard the first argument, which is generally a call to
        // 'halide_printf'.
        print_assignment(op->type, print_expr(op->args[1]));
        return;
    } else if (op->name == "fast_inverse_f32") {
        print_expr(make_one(op->type) / op->args[0]);
        return;
    } else if (op->name == "fast_inverse_sqrt_f32") {
        print_expr(make_one(op->type) / sqrt(op->args[0]));
        return;
    } else if (op->name == "pow_f32") {
        if (can_prove(op->args[0] > 0)) {
            ostringstream rhs;
            rhs << "pow(" << print_expr(op->args[0]) << ", " << print_expr(op->args[1]) << ")";
            print_assignment(op->type, rhs.str());
            return;
        } else {
            ostringstream base;
            string a = print_expr(op->args[0]);
            string b = print_expr(op->args[1]);
            base << "pow(abs(" << a << "), " << b << ")";
            string c = print_assignment(op->type, base.str());
            Expr a_var = is_const(op->args[0]) ? op->args[0] : Variable::make(op->type, a);
            Expr b_var = is_const(op->args[1]) ? op->args[1] : Variable::make(op->type, b);
            Expr c_var = Variable::make(op->type, c);
            // OpenGL isn't required to produce NaNs, so we return
            // zero in the undefined case.
            Expr equiv = select(a_var > 0 || b_var % 2 == 0, c_var,
                                b_var % 2 == 1, -c_var,
                                0.0f);
            print_expr(simplify(equiv));
            return;
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        print_assignment(op->type, print_expr(op->args[0]) + " >> " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::shift_left)) {
        print_assignment(op->type, print_expr(op->args[0]) + " << " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        print_assignment(op->type, "~" + print_expr(op->args[0]));
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        print_assignment(op->type, print_expr(op->args[0]) + " & " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        print_assignment(op->type, print_expr(op->args[0]) + " | " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        print_assignment(op->type, print_expr(op->args[0]) + " ^ " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        print_assignment(op->type, print_expr(op->args[0]) + " / " + print_expr(op->args[1]));
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        print_assignment(op->type, print_expr(op->args[0]) + " % " + print_expr(op->args[1]));
    } else {
        auto it = builtin.find(op->name);
        if (it == builtin.end()) {
            user_error << "GLSL: unknown function '" << op->name << "' encountered.\n";
        }

        ostringstream rhs;
        rhs << it->second << "(";
        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) {
                rhs << ", ";
            }
            rhs << print_expr(op->args[i]);
        }
        rhs << ")";
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_OpenGLCompute_C::visit(const Cast *op) {
    Type value_type = op->value.type();
    // If both types are represented by the same GLSL type, no explicit cast
    // is necessary.
    Type target_type = map_type(op->type);
    if (target_type == map_type(value_type)) {
        Expr value = op->value;
        if (value_type.code() == Type::Float) {
            // float->int conversions may need explicit truncation if an
            // integer type is embedded into a float. (Note: overflows are
            // considered undefined behavior, so we do nothing about values
            // that are out of range of the target type.)
            if (op->type.code() == Type::UInt) {
                value = simplify(floor(value));
            } else if (op->type.code() == Type::Int) {
                value = simplify(trunc(value));
            }
        }
        // FIXME: Overflow is not UB for most Halide types
        // https://github.com/halide/Halide/issues/4975
        value.accept(this);
    } else {
        print_assignment(target_type, print_type(target_type) + "(" + print_expr(op->value) + ")");
    }
}

void CodeGen_OpenGLCompute_C::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The OpenGLCompute backend does not support the gpu_lanes() scheduling directive.";

    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        internal_assert((loop->for_type == ForType::GPUBlock) ||
                        (loop->for_type == ForType::GPUThread))
            << "kernel loop must be either gpu block or gpu thread\n";
        internal_assert(is_const_zero(loop->min));

        debug(4) << "loop extent is " << loop->extent << "\n";
        //
        //  Need to extract workgroup size.
        //
        int index = thread_loop_workgroup_index(loop->name);
        if (index >= 0) {
            const IntImm *int_limit = loop->extent.as<IntImm>();
            user_assert(int_limit != nullptr) << "For OpenGLCompute workgroup size must be a constant integer.\n";
            int new_workgroup_size = int_limit->value;
            user_assert(workgroup_size[index] == 0 ||
                        workgroup_size[index] == new_workgroup_size)
                << "OpenGLCompute requires all gpu kernels have same workgroup size, "
                << "but two different ones were encountered " << workgroup_size[index]
                << " and " << new_workgroup_size
                << " in dimension " << index << ".\n";
            workgroup_size[index] = new_workgroup_size;
            debug(4) << "Workgroup size for index " << index << " is " << workgroup_size[index] << "\n";
        }

        stream << get_indent() << print_type(Int(32)) << " " << print_name(loop->name)
               << " = int(" << simt_intrinsic(loop->name) << ");\n";

        loop->body.accept(this);

    } else {
        user_assert(loop->for_type != ForType::Parallel)
            << "Cannot use parallel loops inside OpenGLCompute kernel\n";
        CodeGen_C::visit(loop);
    }
}

void CodeGen_OpenGLCompute_C::visit(const Ramp *op) {
    if (op->lanes > 4) {
        internal_error << "GLSL: ramp lanes " << op->lanes << " is not supported\n";
    }

    ostringstream rhs;
    // Print the sequence vec(0, 1, 2, ...).
    rhs << print_type(op->type) << "(";
    for (int i = 0; i < op->type.lanes(); i++) {
        rhs << i;
        if (i != op->type.lanes() - 1) {
            rhs << ", ";
        }
    }
    rhs << ")";

    // Multiply by the stride and add the base.
    rhs << " * " << print_expr(op->stride) << " + " << print_expr(op->base);

    print_assignment(op->type, rhs.str());
}

void CodeGen_OpenGLCompute_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);
    ostringstream oss;
    oss << print_type(op->type.with_lanes(op->lanes)) << "(" << id_value << ")";
    print_assignment(op->type.with_lanes(op->lanes), oss.str());
}

void CodeGen_OpenGLCompute_C::visit(const Load *op) {
    user_assert(is_const_one(op->predicate)) << "GLSL: predicated load is not supported.\n";
    // https://github.com/halide/Halide/issues/4975

    string name = print_name(op->name);
    if (!allocations.contains(op->name)) {
        name += ".data";
    }

    // If the index is scalar, just index the buffer using the index.
    if (op->type.is_scalar()) {
        internal_assert(!buffer_is_vector[op->name]);
        string index_id = print_expr(op->index);
        string rhs = name + "[" + index_id + "]";
        print_assignment(op->type, rhs);
        return;
    }

    // If this is a dense vector load and the buffer has a vector base type,
    // then index the buffer using the base of the ramp divided by the number
    // of lanes.
    Expr ramp_base = strided_ramp_base(op->index);
    if (ramp_base.defined() && buffer_is_vector[op->name]) {
        string index_id = print_expr(ramp_base / op->type.lanes());
        string rhs = name + "[" + index_id + "]";
        print_assignment(op->type, rhs);
        return;
    }

    // Gather vector elements.
    internal_assert(op->type.is_vector());
    internal_assert(!buffer_is_vector[op->name]);
    string index_id = print_expr(op->index);
    string rhs = print_type(op->type) + "(";
    for (int i = 0; i < op->type.lanes(); i++) {
        rhs += name + "[" + index_id + "[" + std::to_string(i) + "]]";
        if (i != op->type.lanes() - 1) {
            rhs += ", ";
        }
    }
    rhs += ")";
    print_assignment(op->type, rhs);
}

void CodeGen_OpenGLCompute_C::visit(const Store *op) {
    user_assert(is_const_one(op->predicate)) << "GLSL: predicated store is not supported.\n";
    // https://github.com/halide/Halide/issues/4975

    string name = print_name(op->name);
    if (!allocations.contains(op->name)) {
        name += ".data";
    }

    string value_id = print_expr(op->value);

    // If the index is scalar, just index the buffer using the index.
    if (op->value.type().is_scalar()) {
        internal_assert(!buffer_is_vector[op->name]);
        string index_id = print_expr(op->index);
        stream << get_indent() << name << "[" << index_id << "] = ";
        stream << value_id << ";\n";

        // Need a cache clear on stores to avoid reusing stale loaded
        // values from before the store.
        cache.clear();
        return;
    }

    // If this is a dense vector store and the buffer has a vector base type,
    // then index the buffer using the base of the ramp divided by the number
    // of lanes.
    Expr ramp_base = strided_ramp_base(op->index);
    if (ramp_base.defined() && buffer_is_vector[op->name]) {
        string index_id = print_expr(ramp_base / op->value.type().lanes());
        stream << get_indent() << name << "[" << index_id << "] = ";
        stream << value_id << ";\n";

        // Need a cache clear on stores to avoid reusing stale loaded
        // values from before the store.
        cache.clear();
        return;
    }

    // Scatter vector elements.
    internal_assert(op->value.type().is_vector());
    internal_assert(!buffer_is_vector[op->name]);
    string index_id = print_expr(op->index);
    for (int i = 0; i < op->value.type().lanes(); i++) {
        string sub_index_id = index_id + "[" + std::to_string(i) + "]";
        stream << get_indent() << name << "[" << sub_index_id << "] = ";
        stream << value_id << "[" << std::to_string(i) << "];\n";
    }

    // Need a cache clear on stores to avoid reusing stale loaded
    // values from before the store.
    cache.clear();
}

void CodeGen_OpenGLCompute_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    if (op->type.is_scalar()) {
        rhs << cond << " ? " << true_val << " : " << false_val;
    } else {
        rhs << print_type(op->type) << "(";
        for (int i = 0; i < op->type.lanes(); i++) {
            string index = "[" + std::to_string(i) + "]";
            rhs << cond << index << " ? "
                << true_val << index << " : "
                << false_val << index;
            if (i != op->type.lanes() - 1) {
                rhs << ", ";
            }
        }
        rhs << ")";
    }
    print_assignment(op->type, rhs.str());
}

class CodeGen_OpenGLCompute_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_OpenGLCompute_Dev(const Target &target);

    // CodeGen_GPU_Dev interface
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override {
        return "openglcompute";
    }
    bool kernel_run_takes_types() const override {
        return true;
    }

protected:
    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_OpenGLCompute_C glc;
};

CodeGen_OpenGLCompute_Dev::CodeGen_OpenGLCompute_Dev(const Target &target)
    : glc(src_stream, target) {
}

void CodeGen_OpenGLCompute_Dev::add_kernel(Stmt s,
                                           const string &name,
                                           const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_OpenGLCompute_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    glc.add_kernel(s, name, args);
}

namespace {
class FindSharedAllocations : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Allocate *op) override {
        op->body.accept(this);
        if (op->memory_type == MemoryType::GPUShared) {
            allocs.push_back(op);
        }
    }

public:
    vector<const Allocate *> allocs;
};

// Check if all loads and stores to the member 'buffer' are dense, aligned, and
// have the same number of lanes. If this is indeed the case then the 'lanes'
// member stores the number of lanes in those loads and stores.
class CheckAlignedDenseVectorLoadStore : public IRVisitor {
public:
    // True if all loads and stores from the buffer are dense, aligned, and all
    // have the same number of lanes, false otherwise.
    bool are_all_dense = true;

    // The number of lanes in the loads and stores. If the number of lanes is
    // variable, then are_all_dense is set to false regardless, and this value
    // is undefined. Initially set to -1 before any dense operation is
    // discovered.
    int lanes = -1;

    CheckAlignedDenseVectorLoadStore(string buffer)
        : buffer(std::move(buffer)) {
    }

private:
    // The name of the buffer to check.
    string buffer;

    using IRVisitor::visit;

    void visit(const Load *op) override {
        IRVisitor::visit(op);

        if (op->name != buffer) {
            return;
        }

        if (op->type.is_scalar()) {
            are_all_dense = false;
            return;
        }

        Expr ramp_base = strided_ramp_base(op->index);
        if (!ramp_base.defined()) {
            are_all_dense = false;
            return;
        }

        if ((op->alignment.modulus % op->type.lanes() != 0) ||
            (op->alignment.remainder % op->type.lanes() != 0)) {
            are_all_dense = false;
            return;
        }

        if (lanes != -1 && op->type.lanes() != lanes) {
            are_all_dense = false;
            return;
        }

        lanes = op->type.lanes();
    }

    void visit(const Store *op) override {
        IRVisitor::visit(op);

        if (op->name != buffer) {
            return;
        }

        if (op->value.type().is_scalar()) {
            are_all_dense = false;
            return;
        }

        Expr ramp_base = strided_ramp_base(op->index);
        if (!ramp_base.defined()) {
            are_all_dense = false;
            return;
        }

        if ((op->alignment.modulus % op->value.type().lanes() != 0) ||
            (op->alignment.remainder % op->value.type().lanes() != 0)) {
            are_all_dense = false;
            return;
        }

        if (lanes != -1 && op->value.type().lanes() != lanes) {
            are_all_dense = false;
            return;
        }

        lanes = op->value.type().lanes();
    }
};
}  // namespace

void CodeGen_OpenGLCompute_C::add_kernel(const Stmt &s,
                                         const string &name,
                                         const vector<DeviceArgument> &args) {

    debug(2) << "Adding OpenGLCompute kernel " << name << "\n";
    cache.clear();

    if (target.os == Target::Android) {
        stream << "#version 310 es\n"
               << "#extension GL_ANDROID_extension_pack_es31a : require\n";
    } else if (target.has_feature(Target::EGL)) {
        stream << "#version 310 es\n";
    } else {
        stream << "#version 430\n";
    }
    stream << "float float_from_bits(int x) { return intBitsToFloat(int(x)); }\n";
    stream << "#define halide_unused(x) (void)(x)\n";

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer) {
            //
            // layout(binding = 10) buffer buffer10 {
            //     vec3 data[];
            // } inBuffer;
            //
            CheckAlignedDenseVectorLoadStore check_dense(args[i].name);
            s.accept(&check_dense);
            int lanes = check_dense.are_all_dense ? check_dense.lanes : 1;
            buffer_is_vector[args[i].name] = lanes > 1;
            stream << "layout(binding=" << i << ")"
                   << " buffer buffer" << i << " { "
                   << print_type(args[i].type.with_lanes(lanes)) << " data[]; } "
                   << print_name(args[i].name) << ";\n";
        } else {
            stream << "layout(location = " << i << ") uniform " << print_type(args[i].type)
                   << " " << print_name(args[i].name) << ";\n";
        }
    }

    // Find all the shared allocations and declare them at global scope.
    FindSharedAllocations fsa;
    s.accept(&fsa);
    for (const Allocate *op : fsa.allocs) {
        internal_assert(op->extents.size() == 1 && is_const(op->extents[0]));
        stream << "shared "
               << print_type(op->type) << " "
               << print_name(op->name) << "["
               << op->extents[0] << "];\n";
    }

    // We'll figure out the workgroup size while traversing the stmt
    workgroup_size[0] = 0;
    workgroup_size[1] = 0;
    workgroup_size[2] = 0;

    stream << "void main()\n{\n";
    indent += 2;
    print(s);
    indent -= 2;
    stream << "}\n";

    // Declare the workgroup size.
    indent += 2;
    stream << "layout(local_size_x = " << workgroup_size[0];
    if (workgroup_size[1] > 1) {
        stream << ", local_size_y = " << workgroup_size[1];
    }
    if (workgroup_size[2] > 1) {
        stream << ", local_size_z = " << workgroup_size[2];
    }
    stream << ") in;\n// end of kernel " << name << "\n";
    indent -= 2;
}

void CodeGen_OpenGLCompute_Dev::init_module() {
    src_stream.str("");
    src_stream.clear();
    cur_kernel_name = "";
}

void CodeGen_OpenGLCompute_C::visit(const Allocate *op) {
    debug(2) << "OpenGLCompute: Allocate " << op->name << " of type " << op->type << " on device\n";

    stream << get_indent();
    Allocation alloc;
    alloc.type = op->type;
    allocations.push(op->name, alloc);

    internal_assert(!op->extents.empty());
    Expr extent = 1;
    for (const Expr &e : op->extents) {
        extent *= e;
    }
    extent = simplify(extent);
    internal_assert(is_const(extent));

    if (op->memory_type != MemoryType::GPUShared) {
        stream << "{\n";
        indent += 2;
        stream << get_indent();
        // Shared allocations were already declared at global scope.
        stream << print_type(op->type) << " "
               << print_name(op->name) << "["
               << op->extents[0] << "];\n";
    }
    op->body.accept(this);

    if (op->memory_type != MemoryType::GPUShared) {
        indent -= 2;
        stream << get_indent() << "}\n";
    }

    buffer_is_vector[op->name] = op->type.is_vector();
}

void CodeGen_OpenGLCompute_C::visit(const Free *op) {
    debug(2) << "OpenGLCompute: Free on device for " << op->name << "\n";

    allocations.pop(op->name);
}

void CodeGen_OpenGLCompute_C::visit(const Evaluate *op) {
    if (is_const(op->value)) {
        return;
    }
    print_expr(op->value);
}

void CodeGen_OpenGLCompute_C::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        // GL seems to interpret some large int immediates as uints.
        id = "int(" + std::to_string(op->value) + ")";
    } else {
        id = print_type(op->type) + "(" + std::to_string(op->value) + ")";
    }
}

vector<char> CodeGen_OpenGLCompute_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "GLSL Compute source:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_OpenGLCompute_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_OpenGLCompute_Dev::dump() {
    std::cerr << src_stream.str() << "\n";
}

std::string CodeGen_OpenGLCompute_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_OpenGLCompute_Dev(const Target &target) {
    return std::make_unique<CodeGen_OpenGLCompute_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "CodeGen_D3D12Compute_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"

#define DEBUG_TYPES (0)

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::sort;
using std::string;
using std::vector;

static ostringstream nil;

CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_Dev(Target t)
    : d3d12compute_c(src_stream, t) {
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space) {
    ostringstream oss;

    // Storage uses packed vector types.
    if (storage && type.lanes() != 1) {
        // NOTE(marcos): HLSL has 'packoffset' but it only applies to constant
        // buffer fields (constant registers (c)); because constant arguments
        // are always converted to 32bit values by the runtime prior to kernel
        // dispatch, there is no need to complicate things with packoffset.
    }

    if (type.is_float()) {
        switch (type.bits()) {
        case 16:
            // 16-bit floating point value. This data type is provided only for language compatibility.
            // Direct3D 10 shader targets map all half data types to float data types.
            // A half data type cannot be used on a uniform global variable (use the /Gec flag if this functionality is desired).
            oss << "half";
            break;
        case 32:
            oss << "float";
            break;
        case 64:
            // "64-bit floating point value. You cannot use double precision values as inputs and outputs for a stream.
            //  To pass double precision values between shaders, declare each double as a pair of uint data types.
            //  Then, use the asdouble function to pack each double into the pair of uints and the asuint function to
            //  unpack the pair of uints back into the double."
            user_error << "HLSL (SM 5.1) does not have transparent support for 'double' types.\n";
            oss << "double";
            break;
        default:
            user_error << "Can't represent a float with this many bits in HLSL (SM 5.1): " << type << "\n";
        }
    } else {
        switch (type.bits()) {
        case 1:
            oss << "bool";
            break;
        case 8:
        case 16:
        case 32:
            if (type.is_uint()) oss << 'u';
            oss << "int";
#if DEBUG_TYPES
            oss << type.bits();
#endif
            break;
        case 64:
            user_error << "HLSL (SM 5.1) does not support 64-bit integers.\n";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in HLSL (SM 5.1): " << type << "\n";
        }
    }
    switch (type.lanes()) {
    case 1:
        break;
    case 2:
    case 3:
    case 4:
#if DEBUG_TYPES
        oss << "_(";
#endif
        oss << type.lanes();
#if DEBUG_TYPES
        oss << ")";
#endif
        break;
    case 8:
    case 16:
        // TODO(marcos): are there 8-wide and 16-wide types in HLSL?
        // (CodeGen_GLSLBase seems to happily generate invalid vector types)
    default:
        user_error << "Unsupported vector width in HLSL (SM 5.1): " << type << "\n";
    }

    if (space == AppendSpace) {
        oss << ' ';
    }

    return oss.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_type(Type type, AppendSpaceIfNeeded space) {
    return print_type_maybe_storage(type, false, space);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_storage_type(Type type) {
    return print_type_maybe_storage(type, true, DoNotAppendSpace);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinterpret(Type type, Expr e) {
    return print_reinterpret_cast(type, print_expr(e));
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return "tid_in_tgroup.x";
    } else if (ends_with(name, ".__thread_id_y")) {
        return "tid_in_tgroup.y";
    } else if (ends_with(name, ".__thread_id_z")) {
        return "tid_in_tgroup.z";
    } else if (ends_with(name, ".__thread_id_w")) {
        user_error << "HLSL (SM5.1) does not support more than three dimensions for compute kernel threads.\n";
    } else if (ends_with(name, ".__block_id_x")) {
        return "tgroup_index.x";
    } else if (ends_with(name, ".__block_id_y")) {
        return "tgroup_index.y";
    } else if (ends_with(name, ".__block_id_z")) {
        return "tgroup_index.z";
    } else if (ends_with(name, ".__block_id_w")) {
        user_error << "HLSL (SM5.1) does not support more than three dimensions for compute dispatch groups.\n";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}  // namespace

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Evaluate *op) {
    if (is_const(op->value)) return;
    print_expr(op->value);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_extern_call(const Call *op) {
    internal_assert(!function_takes_user_context(op->name));

    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
    }
    ostringstream rhs;
    rhs << op->name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits) - 1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const For *loop) {
    if (!is_gpu_var(loop->name)) {
        user_assert(loop->for_type != ForType::Parallel) << "Cannot use parallel loops inside D3D12Compute kernel\n";
        CodeGen_C::visit(loop);
        return;
    }

    internal_assert((loop->for_type == ForType::GPUBlock) ||
                    (loop->for_type == ForType::GPUThread))
        << "kernel loop must be either gpu block or gpu thread\n";
    internal_assert(is_zero(loop->min));

    stream << get_indent() << print_type(Int(32)) << " " << print_name(loop->name)
           << " = " << simt_intrinsic(loop->name) << ";\n";

    loop->body.accept(this);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Ramp *op) {
    ostringstream rhs;
    rhs << print_expr(op->base)
        << " + "
        << print_expr(op->stride)
        << " * "
        << print_type(op->type.with_lanes(op->lanes))
        << "(";

    rhs << "0";
    for (int i = 1; i < op->lanes; ++i) {
        rhs << ", " << i;
    }

    rhs << ")";
    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Broadcast *op) {
    string id_value = print_expr(op->value);
    user_assert(op->value.type().lanes() == 1) << "Broadcast source must be 1-wide.\n";

    ostringstream rhs;
    rhs << print_type(op->type.with_lanes(op->lanes))
        << "(";
    for (int i = 0; i < op->lanes; ++i) {
        rhs << id_value;
        if (i < op->lanes - 1)
            rhs << ", ";
    }
    rhs << ")";

    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        // Halide only ever needs threadgroup memory fences:
        // NOTE(marcos): using "WithGroupSync" here just to be safe, as a
        // simple "GroupMemoryBarrier" is probably too relaxed for Halide
        // (also note we need to return an integer)
        stream << get_indent() << "GroupMemoryBarrierWithGroupSync();\n";
        print_assignment(op->type, "0");
    } else {
        CodeGen_C::visit(op);
    }
}

namespace {

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp_one(Expr e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    if (is_one(r->stride)) {
        return r->base;
    }

    return Expr();
}

template<typename T>
string hex_literal(T value) {
    ostringstream hex;
    hex << "0x" << std::uppercase << std::setfill('0') << std::setw(8) << std::hex
        << value;
    return hex.str();
}

}  // namespace

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Load *op) {
    user_assert(is_one(op->predicate)) << "Predicated load is not supported inside D3D12Compute kernel.\n";

    // __shared[x] is always uint(32): must reinterpret/unpack bits...
    if (op->name == "__shared") {
        ostringstream rhs;
        internal_assert(allocations.contains(op->name));
        // no ramps when accessing shared memory...
        Expr ramp_base = is_ramp_one(op->index);
        internal_assert(!ramp_base.defined());
        // shared memory in Halide is represented as a byte buffer
        // but the 'op->index' is actually in terms of elements...
        // to complicate things, HLSL (SM 5.1) only supports 32bit
        // words (int/uint/float) as groupshared types...
        internal_assert(allocations.get(op->name).type == UInt(8));
        internal_assert(op->type.lanes() == 1);

        string id_index = print_expr(op->index);
        internal_assert(op->type.bits() <= 32);
        Type promoted = op->type.with_bits(32);
        rhs << "as" << print_type(promoted)
            << "("
            << print_name(op->name)
            << "[" << id_index << "]"
            << ")";
#if 0
        // NOTE(marcos): let's keep this block of code here (disabled) in case
        // we need to "emulate" byte/short packing in shared memory (recall that
        // the smallest type granularity in HLSL SM 5.1 allows is 32bit types):
        if (op->type.bits() == 32) {
            // loading a 32bit word? great! just reinterpret as float/int/uint
            rhs << "as" << print_type(op->type.element_of())
                << "("
                << print_name(op->name)
                << "[" << print_expr(op->index) << "]"
                << ")";
        } else {
            // not a 32bit word? hell ensues:
            internal_assert(op->type.bits() < 32);
            // must map element index to uint word array index:
            ostringstream index;
            auto bits = op->type.bits();
            auto divisor = (32 / bits);
            auto i = print_expr(op->index);
            index << i << " / " << divisor;
            ostringstream word;
            word << print_name(op->name)
                 << "[" + index.str() + "]";
            // now mask the appropriate bits:
            ostringstream mask;
            mask << "("
                 << hex_literal((1 << bits) - 1)
                 << " << "
                 << "(" << bits << "*(" << i << " % " << divisor << " ))"
                 << ")";
            // extract the correct bits from the word
            ostringstream cut;
            cut << "("
                << word.str()
                << " & "
                << mask.str()
                << ")"
                << " >> "
                << "(" << bits << "*(" << i << " % " << divisor << " ))";
            // if it is signed, need to propagate sign... shift-up then down:
            if (op->type.is_int()) {
                rhs << "asint"
                    << "("
                    << cut.str()
                    << " << "
                    << (32 - bits)
                    << ")"
                    << " >> "
                    << (32 - bits);
            } else {
                rhs << cut.str();
            }
        }
#endif
        print_assignment(op->type, rhs.str());
        return;
    }

    // If we're loading a contiguous ramp, "unroll" the ramp into loads:
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(op->type.is_vector());

        ostringstream rhs;
        rhs << print_type(op->type)
            << "(";
        const int lanes = op->type.lanes();
        for (int i = 0; i < lanes; ++i) {
            rhs << print_name(op->name)
                << "["
                << print_expr(ramp_base)
                << "+" << i
                << "]";
            if (i < lanes - 1)
                rhs << ", ";
        }
        rhs << ")";

        print_assignment(op->type, rhs.str());

        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    bool type_cast_needed = !(allocations.contains(op->name) &&
                              allocations.get(op->name).type == op->type);

    ostringstream rhs;
    if (type_cast_needed) {
        ostringstream element;
        element << print_name(op->name)
                << "[" << id_index << "]";
        Type target_type = op->type;
        Type source_type = allocations.get(op->name).type;
        rhs << print_cast(target_type, source_type, element.str());
    } else {
        rhs << print_name(op->name);
        rhs << "[" << id_index << "]";
    }

    std::map<string, string>::iterator cached = cache.find(rhs.str());
    if (cached != cache.end()) {
        id = cached->second;
        return;
    }

    if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(op->type.is_vector());

        // This has to be underscore as print_name prepends an underscore to
        // names without one and that results in a name mismatch if a Load
        // appears as the value of a Let
        id = unique_name('_');
        cache[rhs.str()] = id;

        stream << get_indent() << print_type(op->type)
               << " " << id << ";\n";

        for (int i = 0; i < op->type.lanes(); ++i) {
            stream << get_indent() << id << "[" << i << "] = "
                   << print_type(op->type.element_of())
                   << "("
                   << print_name(op->name)
                   << "[" << id_index << "[" << i << "]]"
                   << ");\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Store *op) {
    user_assert(is_one(op->predicate)) << "Predicated store is not supported inside D3D12Compute kernel.\n";

    Type value_type = op->value.type();

    // __shared[x] is always uint(32): must reinterpret/pack bits...
    if (op->name == "__shared") {
        internal_assert(value_type.bits() <= 32);
        ostringstream rhs;
        rhs << print_name(op->name)
            << "[" << print_expr(op->index) << "]"
            << " = "
            << print_reinterpret(UInt(32), op->value)
            << ";\n";
        stream << get_indent() << rhs.str();
#if 0
        // NOTE(marcos): let's keep this block of code here (disabled) in case
        // we need to "emulate" byte/short packing in shared memory (recall that
        // the smallest type granularity in HLSL SM 5.1 allows is 32bit types):
        if (value_type.bits() == 32) {
            // storing a 32bit word? great! just reinterpret value to uint32:
            stream << print_name(op->name)
                    << "[" << print_expr(op->index) << "]"
                    << " = "
                    << print_reinterpret(UInt(32), op->value)
                    << ";\n";
        } else {
            // nightmare:
            internal_assert(value_type.bits() < 32);
            // must map element index to uint word array index:
            ostringstream index;
            auto bits = value_type.bits();
            auto divisor = (32 / bits);
            auto i = print_expr(op->index);
            index << i << " / " << divisor;
            ostringstream word;
            word << print_name(op->name)
                 << "[" + index.str() + "]";
            // now mask the appropriate bits:
            ostringstream mask;
            mask << "("
                 << hex_literal((1 << bits) - 1)
                 << " << "
                 << "(" << bits << "*(" << i << " % " << divisor << " ))"
                 << ")";
            // apply the mask to the rhs value:
            ostringstream value;
            value << "("
                  << mask.str()
                  << " & "
                  << "(" << print_expr(op->value) << ")"
                  << ")";

            // the performance impact of atomic operations on shared memory is
            // not well documented... here is something:
            // https://stackoverflow.com/a/19548723
            stream << get_indent() << "InterlockedAnd(" << word.str() << ", " << "~" << mask.str() << ");\n";
            stream << get_indent() << "InterlockedXor(" << word.str() << ", " << value.str() << ");\n";
        }
#endif
        return;
    }

    // If we're writing a contiguous ramp, "unroll" the ramp into stores:
    Expr ramp_base = is_ramp_one(op->index);
    if (ramp_base.defined()) {
        internal_assert(value_type.is_vector());

        int lanes = value_type.lanes();
        for (int i = 0; i < lanes; ++i) {
            // TODO(marcos): this indentation looks funny in the generated code
            ostringstream rhs;
            rhs << print_name(op->name)
                << "["
                << print_expr(ramp_base)
                << " + " << i
                << "]"
                << " = "
                << print_expr(op->value)
                << "["
                << i
                << "]"
                << ";\n";
            stream << get_indent() << rhs.str();
        }
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(value_type.is_vector());

        for (int i = 0; i < value_type.lanes(); ++i) {
            ostringstream rhs;
            rhs << print_name(op->name)
                << "["
                << print_expr(op->index) << "[" << i << "]"
                << "]"
                << " = "
                << print_expr(op->value) << "[" << i << "];\n";
            stream << get_indent() << rhs.str();
        }
    } else {
        ostringstream rhs;
        rhs << print_name(op->name)
            << "[" << print_expr(op->index) << "]"
            << " = "
            << print_expr(op->value)
            << ";\n";
        stream << get_indent() << rhs.str();
    }

    cache.clear();
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    rhs << print_type(op->type)
        << "(" << cond
        << " ? " << true_val
        << " : " << false_val
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Allocate *op) {

    if (op->name == "__shared") {
        // Already handled
        op->body.accept(this);
    } else {
        open_scope();

        debug(2) << "Allocate " << op->name << " on device\n";

        debug(3) << "Pushing allocation called " << op->name << " onto the symbol table\n";

        // Allocation is not a shared memory allocation, just make a local declaration.
        // It must have a constant size.
        int32_t size = op->constant_allocation_size();
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported on the gpu. "
            << "Try storing into shared memory instead.";

        stream << get_indent() << print_storage_type(op->type) << ' '
               << print_name(op->name) << "[" << size << "];\n";
        stream << get_indent();

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        op->body.accept(this);

        // Should have been freed internally
        internal_assert(!allocations.contains(op->name));

        close_scope("alloc " + print_name(op->name));
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Free *op) {
    if (op->name == "__shared") {
        return;
    } else {
        // Should have been freed internally
        internal_assert(allocations.contains(op->name));
        allocations.pop(op->name);
        stream << get_indent();
    }
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_assignment(Type type, const string &rhs) {
    string rhs_modified = print_reinforced_cast(type, rhs);
    return CodeGen_C::print_assignment(type, rhs_modified);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_vanilla_cast(Type type, string value_expr) {
    ostringstream ss;
    ss << print_type(type) << "(" << value_expr << ")";
    return ss.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinforced_cast(Type type, string value_expr) {
    if (type.is_float()) {
        return print_vanilla_cast(type, value_expr);
    }

    if (type.is_bool()) {
        return print_vanilla_cast(type, value_expr);
    }

    if (type.bits() == 32) {
        return print_reinterpret_cast(type, value_expr);
    }

    // HLSL SM 5.1 only supports 32bit integer types; smaller integer types have
    // to be placed in 32bit integers, with special attention to signed integers
    // that require propagation of the sign bit (MSB):
    // a) for signed types: shift-up then shift-down
    // b) for unsigned types: simply mask the LSB (but shift-up and down also works)
    ostringstream sl;
    sl << "(" << value_expr << ")"
       << " << "
       << "(" << (32 - type.bits()) << ")";  // 1. shift-up to MSB
    ostringstream rsr;
    rsr << print_reinterpret_cast(type, sl.str())  // 2. reinterpret bits
        << " >> " << (32 - type.bits());           // 3. shift-down to LSB
    return rsr.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinterpret_cast(Type type, string value_expr) {
    type = type.element_of();

    string cast_expr;
    cast_expr += "as";
    switch (type.code()) {
    case halide_type_uint:
        cast_expr += "uint";
        break;
    case halide_type_int:
        cast_expr += "int";
        break;
    case halide_type_float:
        cast_expr += "float";
        break;
    case halide_type_handle:
    default:
        cast_expr = "BADCAST";
        user_error << "Invalid reinterpret cast.\n";
        break;
    }
    cast_expr += "(" + value_expr + ")";
    return cast_expr;
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_cast(Type target_type, Type source_type, string value_expr) {
    // casting to or from a float type? just use the language cast:
    if (target_type.is_float() || source_type.is_float()) {
        return print_vanilla_cast(target_type, value_expr);
    }

    // casting to or from a bool type? just use the language cast:
    if (target_type.is_bool() || source_type.is_bool()) {
        return print_vanilla_cast(target_type, value_expr);
    }

    // let the integer cast zoo begin...
    internal_assert(!target_type.is_float());
    internal_assert(!source_type.is_float());

    // HLSL (SM 5.1) only supports 32bit integers (signed and unsigned)...
    // integer downcasting-to (or upcasting-from) lower bit integers require
    // some emulation in code...
    internal_assert(target_type.bits() >= 8);
    internal_assert(source_type.bits() >= 8);
    internal_assert(target_type.bits() <= 32);
    internal_assert(source_type.bits() <= 32);
    internal_assert(target_type.bits() % 8 == 0);
    internal_assert(source_type.bits() % 8 == 0);

    // Case 1: source and target both have the same "signess"
    bool same_signess = false;
    same_signess |= target_type.is_int() && source_type.is_int();
    same_signess |= target_type.is_uint() && source_type.is_uint();
    if (same_signess) {
        ostringstream ss;
        if (target_type.bits() >= source_type.bits()) {
            // target has enough bits to fully accommodate the source:
            // it's a no-op, but we print a vanilla cast for clarity:
            ss << "(" << print_vanilla_cast(target_type, value_expr) << ")";
        } else {
            // for signed types: shift-up then shift-down
            // for unsigned types: mask the target LSB (but shift-up and down also works)
            ss << "("
               << "(" << value_expr << ")"
               << " << "
               << "(" << (32 - target_type.bits()) << ")"  // 1. shift-up to MSB
               << ")"
               << " >> " << (32 - target_type.bits());  // 2. shift-down to LSB
        }
        return ss.str();
    }

    // Case 2: casting from a signed source to an unsigned target
    if (source_type.is_int() && target_type.is_uint()) {
        // reinterpret resulting bits as uint(32):
        string masked = value_expr;
        if (target_type.bits() < 32) {
            masked = "(" + value_expr + ")" + " & " + hex_literal((1 << target_type.bits()) - 1);  // mask target LSB
        }
        return print_reinterpret_cast(target_type, masked);
    }

    // Case 3: casting from an unsigned source to a signed target
    internal_assert(source_type.is_uint());
    internal_assert(target_type.is_int());
    ostringstream ss;
    if (target_type.bits() > source_type.bits()) {
        // target has enough bits to fully accommodate the source:
        // it's a no-op, but we print a vanilla cast for clarity:
        ss << "(" << print_vanilla_cast(target_type, value_expr) << ")";
    } else {
        // shift-up, reinterpret as int (target_type), then shift-down
        ss << print_reinforced_cast(target_type, value_expr);
    }
    return ss.str();
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Cast *op) {
    Type target_type = op->type;
    Type source_type = op->value.type();
    string value_expr = print_expr(op->value);

    string cast_expr = print_cast(target_type, source_type, print_expr(op->value));

    print_assignment(target_type, cast_expr);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Atomic *op)
{
    // TODO: atomics
    user_assert(false) << "Atomics operations are not supported inside D3D12Compute kernel.\n";
}

void CodeGen_D3D12Compute_Dev::add_kernel(Stmt s,
                                          const string &name,
                                          const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_D3D12Compute_Dev::compile " << name << "\n";

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    d3d12compute_c.add_kernel(s, name, args);
}

namespace {
struct BufferSize {
    string name;
    size_t size;

    BufferSize()
        : size(0) {
    }
    BufferSize(string name, size_t size)
        : name(name), size(size) {
    }

    bool operator<(const BufferSize &r) const {
        return size < r.size;
    }
};
}  // namespace

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::add_kernel(Stmt s,
                                                                  const string &name,
                                                                  const vector<DeviceArgument> &args) {

    debug(2) << "Adding D3D12Compute kernel " << name << "\n";

    // Figure out which arguments should be passed in constant.
    // Such arguments should be:
    // - not written to,
    // - loads are block-uniform,
    // - constant size,
    // - and all allocations together should be less than the max constant
    //   buffer size given by D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT*(4*sizeof(float))
    // The last condition is handled via the preprocessor in the kernel
    // declaration.
    vector<BufferSize> constants;
    auto isConstantBuffer = [&s](const DeviceArgument &arg) {
        return arg.is_buffer && CodeGen_GPU_Dev::is_buffer_constant(s, arg.name) && arg.size > 0;
    };
    for (auto &arg : args) {
        if (isConstantBuffer(arg)) {
            constants.push_back(BufferSize(arg.name, arg.size));
        }
    }

    // Sort the constant candidates from smallest to largest. This will put
    // as many of the constant allocations in constant as possible.
    // Ideally, we would prioritize constant buffers by how frequently they
    // are accessed.
    sort(constants.begin(), constants.end());

    // Compute the cumulative sum of the constants.
    for (size_t i = 1; i < constants.size(); i++) {
        constants[i].size += constants[i - 1].size;
    }

    // Find all the shared allocations and declare them at global scope.
    class FindSharedAllocations : public IRVisitor {
        using IRVisitor::visit;
        void visit(const Allocate *op) override {
            op->body.accept(this);
            if (starts_with(op->name, "__shared")) {
                allocs.push_back(op);
            }
        }
        friend class CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C;
        vector<const Allocate *> allocs;
    };
    FindSharedAllocations fsa;
    s.accept(&fsa);
    for (const Allocate *op : fsa.allocs) {
        internal_assert(op->extents.size() == 1);
        // The 'op->type' of shared memory allocations is always uint8 in Halide
        // since shared storaged is considered a "byte buffer"... In D3D12 there
        // is no uint8 type, so we'll have to emulate it with some 32bit type...
        // This will also require pack/unpack logic with bit-masking and aliased
        // type reinterpretation via asfloat()/asuint() in the shader code... :(
        internal_assert(op->type == UInt(8));
        stream << "groupshared"
               << " " << print_type(op->type)  // print_type(uint8) -> uint32
               << " " << print_name(op->name);
        if (is_const(op->extents[0])) {
            std::stringstream ss;
            ss << op->extents[0];
            size_t elements = 0;
            ss >> elements;
            size_t bytesize = elements * sizeof(uint32_t);
            // SM 5.1: 32KB limit for shared memory...
            size_t packing_factor = 1;
            while (bytesize > 32 * 1024) {
                // must pack/unpack elements to/from shared memory...
                elements /= 2;
                bytesize /= 2;
                packing_factor *= 2;
            }
            stream << " [" << elements << "];\n";
            // smallest possible pack type is a byte (no nibbles)
            internal_assert(packing_factor <= 4);
        } else {
            // fill-in __GROUPSHARED_SIZE_IN_BYTES later on when D3DCompile()
            // is invoked in halide_d3d12compute_run(); must get divided by 4
            // since groupshared memory elements have 32bit granularity:
            stream << " [ ( __GROUPSHARED_SIZE_IN_BYTES + 3 ) / 4 ];\n";
        }
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
    }

    // Find and patch situations where the __shared buffer is read before having
    // ever being initialized:

    // NOTE(marcos): it would be cleaner if we could just use an IRVisitor here
    // but in order to find the enclosing Stmt of a Load expression we need to
    // to walk through base Stmt nodes, and only IRMutator has this overload:
    struct FindUninitializedSharedLoads : public IRMutator {
        using IRMutator::mutate;
        using IRMutator::visit;
        virtual Expr visit(const Load *op) override {
            if (op->name == "__shared") {
                if (!latest_store) {
                    // attempting to read from __shared before anything has been
                    // written to it yet!
                    bad_load_expr = op;
                }
            }
            return IRMutator::visit(op);
        }
        virtual Stmt visit(const Store *op) override {
            Stmt store = IRMutator::visit(op);
            if (op->name == "__shared") {
                latest_store = op;
            }
            return store;
        }
        virtual Stmt mutate(const Stmt &stmt) override {
            if (!bad_load_expr) {
                current_stmt = &stmt;
            }
            return IRMutator::mutate(stmt);
        }
        const Stmt *current_stmt = nullptr;
        const Load *bad_load_expr = nullptr;
        const Store *latest_store = nullptr;
    };
    FindUninitializedSharedLoads fusl;
    s = fusl.mutate(s);
    if (fusl.bad_load_expr) {
        debug(1) << "Found a potential load-before-initialization on __shared buffer!\n";
        // use IRMutator to inject a zero-initialization before the load
        struct ZeroInitializeSharedMemory : public IRMutator {
            using IRMutator::mutate;
            virtual Stmt mutate(const Stmt &op) override {
                if (&op != uninitialized_load_stmt) {
                    return IRMutator::mutate(op);
                }

                debug(1) << "Patching __shared buffer with zero-intialization...\n";

                const Load *lop = uninitialized_load_expr;
                Stmt initialization = Store::make(lop->name, Expr(0), lop->index, Parameter(), lop->predicate, ModulusRemainder());
                return Block::make({initialization, op});
            }
            const Stmt *uninitialized_load_stmt = nullptr;
            const Load *uninitialized_load_expr = nullptr;
        };
        ZeroInitializeSharedMemory zism;
        zism.uninitialized_load_stmt = fusl.current_stmt;
        zism.uninitialized_load_expr = fusl.bad_load_expr;
        s = zism.mutate(s);
    }

    // Emit the kernel function preamble (numtreads):

    // Figure out the thread group size by traversing the stmt:
    struct FindThreadGroupSize : public IRVisitor {
        using IRVisitor::visit;
        void visit(const For *loop) override {
            if (!is_gpu_var(loop->name))
                return loop->body.accept(this);
            if (loop->for_type != ForType::GPUThread)
                return loop->body.accept(this);
            internal_assert(is_zero(loop->min));
            int index = thread_loop_workgroup_index(loop->name);
            user_assert(index >= 0) << "Invalid 'numthreads' index for loop variable '" << loop->name << "'.\n";
            // if 'numthreads' for a given dimension can't be determined at code
            // generation time, emit code such that it can be patched some point
            // later when calling D3DCompile() / halide_d3d12compute_run()
            numthreads[index] = 0;  // <-- 0 indicates 'undetermined'
            const IntImm *int_limit = loop->extent.as<IntImm>();
            if (nullptr != int_limit) {
                numthreads[index] = int_limit->value;
                user_assert(numthreads[index] > 0) << "For D3D12Compute, 'numthreads[" << index << "]' values must be greater than zero.\n";
            }
            debug(4) << "Thread group size for index " << index << " is " << numthreads[index] << "\n";
            loop->body.accept(this);
        }
        int thread_loop_workgroup_index(const string &name) {
            string ids[] = {".__thread_id_x",
                            ".__thread_id_y",
                            ".__thread_id_z",
                            ".__thread_id_w"};
            for (auto &id : ids) {
                if (ends_with(name, id)) {
                    return (&id - ids);
                }
            }
            return -1;
        }
        int numthreads[3] = {1, 1, 1};
    };
    FindThreadGroupSize ftg;
    s.accept(&ftg);
    // for undetermined 'numthreads' dimensions, insert placeholders to the code
    // such as '__NUM_TREADS_X' that will later be patched when D3DCompile() is
    // invoked in halide_d3d12compute_run()
    stream << "[ numthreads(";
    (ftg.numthreads[0] > 0) ? (stream << " " << ftg.numthreads[0]) : (stream << " __NUM_TREADS_X ");
    (ftg.numthreads[1] > 0) ? (stream << ", " << ftg.numthreads[1]) : (stream << ", __NUM_TREADS_Y ");
    (ftg.numthreads[2] > 0) ? (stream << ", " << ftg.numthreads[2]) : (stream << ", __NUM_TREADS_Z ");
    stream << ") ]\n";

    // Emit the kernel function prototype:

    stream << "void " << name << "(\n";
    stream << ' ' << "uint3 tgroup_index  : SV_GroupID,\n"
           << ' ' << "uint3 tid_in_tgroup : SV_GroupThreadID";
    for (auto &arg : args) {
        stream << ",\n";
        stream << ' ';
        if (arg.is_buffer) {
            // NOTE(marcos): Passing all buffers as RWBuffers in order to bind
            // all buffers as UAVs since there is no way the runtime can know
            // if a given halide_buffer_t is read-only (SRV) or read-write...
            stream << "RW"
                   << "Buffer"
                   << "<" << print_type(arg.type) << ">"
                   << " " << print_name(arg.name);
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
        } else {
            stream << "uniform"
                   << " " << print_type(arg.type)
                   << " " << print_name(arg.name);
        }
    }
    stream << ")\n";

    // Emit the kernel code:

    open_scope();
    print(s);
    close_scope("kernel " + name);

    for (size_t i = 0; i < args.size(); i++) {
        // Remove buffer arguments from allocation scope
        if (args[i].is_buffer) {
            allocations.pop(args[i].name);
        }
    }

    stream << "\n";
}

void CodeGen_D3D12Compute_Dev::init_module() {
    debug(2) << "D3D12Compute device codegen init_module\n";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // compiler control pragmas
    src_stream
        // Disable innocent warnings:
        // warning X3078 : loop control variable conflicts with a previous declaration in the outer scope; most recent declaration will be used
        << "#pragma warning( disable : 3078 )"
           "\n"
        // warning X3557: loop only executes for 1 iteration(s), forcing loop to unroll
        << "#pragma warning( disable : 3557 )"
           "\n"
        // Disable more serious warnings:
        // TODO(marcos): should revisit the warnings below, as they are likely to impact performance (and possibly correctness too)
        // warning X3556 : integer modulus may be much slower, try using uints if possible
        // TODO(marcos): can we interchangeably replace ints by uints when we have modulo operations in the generated code?
        << "#pragma warning( disable : 3556 )"
           "\n"
        // warning X3571 : pow(f, e) will not work for negative f, use abs(f) or conditionally handle negative values if you expect them
        << "#pragma warning( disable : 3571 )"
           "\n"
        // warning X4714 : sum of temp registers and indexable temp registers times 256 threads exceeds the recommended total 16384.  Performance may be reduced
        << "#pragma warning( disable : 4714 )"
           "\n"
        << "\n";

    // Write out the Halide math functions.
    src_stream
    //<< "namespace {\n"   // HLSL does not support unnamed namespaces...
#if DEBUG_TYPES
        << "#define  int8   int\n"
        << "#define  int16  int\n"
        << "#define  int32  int\n"
        << "#define uint8  uint\n"
        << "#define uint16 uint\n"
        << "#define uint32 uint\n"
        << "\n"
        << "#define  bool_(x)   bool##x\n"
        << "#define  int8_(x)   int##x\n"
        << "#define  int16_(x)  int##x\n"
        << "#define  int32_(x)  int##x\n"
        << "#define uint8_(x)  uint##x\n"
        << "#define uint16_(x) uint##x\n"
        << "#define uint32_(x) uint##x\n"
        << "\n"
        << "#define asint32  asint\n"
        << "#define asuint32 asuint\n"
        << "\n"
#endif
        << "float nan_f32()     { return  1.#IND; } \n"  // Quiet NaN with minimum fractional value.
        << "float neg_inf_f32() { return -1.#INF; } \n"
        << "float inf_f32()     { return +1.#INF; } \n"
        << "#define float_from_bits asfloat \n"
        << "#define sqrt_f32    sqrt   \n"
        << "#define sin_f32     sin    \n"
        << "#define cos_f32     cos    \n"
        << "#define exp_f32     exp    \n"
        << "#define log_f32     log    \n"
        << "#define abs_f32     abs    \n"
        << "#define floor_f32   floor  \n"
        << "#define ceil_f32    ceil   \n"
        << "#define round_f32   round  \n"
        << "#define trunc_f32   trunc  \n"
        << "#define pow_f32     pow    \n"
        << "#define asin_f32    asin   \n"
        << "#define acos_f32    acos   \n"
        << "#define tan_f32     tan    \n"
        << "#define atan_f32    atan   \n"
        << "#define atan2_f32   atan2  \n"
        << "#define sinh_f32    sinh   \n"
        << "#define cosh_f32    cosh   \n"
        << "#define tanh_f32    tanh   \n"
        << "#define asinh_f32(x) (log_f32(x + sqrt_f32(x*x + 1))) \n"
        << "#define acosh_f32(x) (log_f32(x + sqrt_f32(x*x - 1))) \n"
        << "#define atanh_f32(x) (log_f32((1+x)/(1-x))/2) \n"
        << "#define fast_inverse_f32      rcp   \n"
        << "#define fast_inverse_sqrt_f32 rsqrt \n"
        << "\n";
    //<< "}\n"; // close namespace

    src_stream << '\n';

    d3d12compute_c.add_common_macros(src_stream);

    cur_kernel_name = "";
}

vector<char> CodeGen_D3D12Compute_Dev::compile_to_src() {
    string str = src_stream.str();
    debug(1) << "D3D12Compute kernel:\n"
             << str << "\n";
    vector<char> buffer(str.begin(), str.end());
    buffer.push_back(0);
    return buffer;
}

string CodeGen_D3D12Compute_Dev::get_current_kernel_name() {
    return cur_kernel_name;
}

void CodeGen_D3D12Compute_Dev::dump() {
    std::cerr << src_stream.str() << std::endl;
}

std::string CodeGen_D3D12Compute_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace Internal
}  // namespace Halide

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

#include "CSE.h"
#include "CanonicalizeGPUVars.h"
#include "CodeGen_D3D12Compute_Dev.h"
#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "DeviceArgument.h"
#include "EliminateBoolVectors.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

#define DEBUG_TYPES (0)

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::sort;
using std::string;
using std::vector;

namespace {

ostringstream nil;

class CodeGen_D3D12Compute_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_D3D12Compute_Dev(const Target &target);

    /** Compile a GPU kernel into the module. This may be called many times
     * with different kernels, which will all be accumulated into a single
     * source module shared by a given Halide pipeline. */
    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override;

    /** (Re)initialize the GPU kernel module. This is separate from compile,
     * since a GPU device module will often have many kernels compiled into it
     * for a single pipeline. */
    void init_module() override;

    std::vector<char> compile_to_src() override;

    std::string get_current_kernel_name() override;

    void dump() override;

    std::string print_gpu_name(const std::string &name) override;

    std::string api_unique_name() override {
        return "d3d12compute";
    }

    bool kernel_run_takes_types() const override {
        return true;
    }

protected:
    friend struct StoragePackUnpack;

    class CodeGen_D3D12Compute_C : public CodeGen_GPU_C {
    public:
        CodeGen_D3D12Compute_C(std::ostream &s, const Target &t)
            : CodeGen_GPU_C(s, t) {
            integer_suffix_style = IntegerSuffixStyle::HLSL;

#define alias(x, y)                         \
    extern_function_name_map[x "_f16"] = y; \
    extern_function_name_map[x "_f32"] = y; \
    extern_function_name_map[x "_f64"] = y
            alias("sqrt", "sqrt");
            alias("sin", "sin");
            alias("cos", "cos");
            alias("exp", "exp");
            alias("log", "log");
            alias("abs", "abs");
            alias("floor", "floor");
            alias("ceil", "ceil");
            alias("trunc", "trunc");
            alias("pow", "pow");
            alias("asin", "asin");
            alias("acos", "acos");
            alias("tan", "tan");
            alias("atan", "atan");
            alias("atan2", "atan2");
            alias("sinh", "sinh");
            alias("asinh", "asinh");
            alias("cosh", "cosh");
            alias("acosh", "acosh");
            alias("tanh", "tanh");
            alias("atanh", "atanh");

            alias("is_nan", "isnan");
            alias("is_inf", "isinf");
            alias("is_finite", "isfinite");

            alias("fast_inverse", "rcp");
            alias("fast_inverse_sqrt", "rsqrt");
#undef alias
        }
        void add_kernel(Stmt stmt,
                        const std::string &name,
                        const std::vector<DeviceArgument> &args);

    protected:
        friend struct StoragePackUnpack;

        std::string print_type(Type type, AppendSpaceIfNeeded space_option = DoNotAppendSpace) override;
        std::string print_storage_type(Type type);
        std::string print_type_maybe_storage(Type type, bool storage, AppendSpaceIfNeeded space);
        std::string print_reinterpret(Type type, const Expr &e) override;

        std::string print_vanilla_cast(Type type, const std::string &value_expr);
        std::string print_reinforced_cast(Type type, const std::string &value_expr);
        std::string print_cast(Type target_type, Type source_type, const std::string &value_expr);
        std::string print_reinterpret_cast(Type type, const std::string &value_expr);

        std::string print_assignment(Type t, const std::string &rhs) override;

        using CodeGen_GPU_C::visit;
        void visit(const Evaluate *op) override;
        void visit(const Min *) override;
        void visit(const Max *) override;
        void visit(const Div *) override;
        void visit(const Mod *) override;
        void visit(const For *) override;
        void visit(const Ramp *op) override;
        void visit(const Broadcast *op) override;
        void visit(const Call *op) override;
        void visit(const Load *op) override;
        void visit(const Store *op) override;
        void visit(const Select *op) override;
        void visit(const Allocate *op) override;
        void visit(const Free *op) override;
        void visit(const Cast *op) override;
        void visit(const Atomic *op) override;
        void visit(const Shuffle *op) override;
        void visit(const AssertStmt *op) override;
        void visit(const FloatImm *op) override;

        Scope<> groupshared_allocations;
    };

    std::ostringstream src_stream;
    std::string cur_kernel_name;
    CodeGen_D3D12Compute_C d3d12compute_c;
};

CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_Dev(const Target &t)
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

    const int sm = target.get_d3d12compute_capability_lower_bound();

    if (type.is_float()) {
        switch (type.bits()) {
        case 16:
            if (sm >= 62) {
                // SM 6.2+: native 16-bit float with -enable-16bit-types
                oss << "float16_t";
            } else {
                // SM 5.1/6.0/6.1: 'half' is a float32 alias in FXC; use it for language compat.
                oss << "half";
            }
            break;
        case 32:
            if (sm >= 62) {
                oss << "float32_t";
            } else {
                oss << "float";
            }
            break;
        case 64:
            if (sm >= 60) {
                oss << "double";
            } else {
                user_error << "HLSL SM 5.1 does not support 64-bit float. "
                           << "Use target feature d3d12compute_sm60 or higher.\n";
                oss << "double";
            }
            break;
        default:
            user_error << "Can't represent a float with this many bits in HLSL: " << type << "\n";
        }
    } else {
        switch (type.bits()) {
        case 1:
            oss << "bool";
            break;
        case 8:
            if (sm >= 66) {
                // SM 6.6+: native 8-bit integers
                if (type.is_uint()) {
                    oss << "uint8_t";
                } else {
                    oss << "int8_t";
                }
            } else {
                if (type.is_uint()) {
                    oss << "u";
                }
                oss << "int";
#if DEBUG_TYPES
                oss << "8";
#endif
            }
            break;
        case 16:
            if (sm >= 62) {
                // SM 6.2+: native 16-bit integers
                if (type.is_uint()) {
                    oss << "uint16_t";
                } else {
                    oss << "int16_t";
                }
            } else {
                if (type.is_uint()) {
                    oss << "u";
                }
                oss << "int";
#if DEBUG_TYPES
                oss << "16";
#endif
            }
            break;
        case 32:
            if (sm >= 62) {
                if (type.is_uint()) {
                    oss << "uint32_t";
                } else {
                    oss << "int32_t";
                }
            } else {
                if (type.is_uint()) {
                    oss << "u";
                }
                oss << "int";
#if DEBUG_TYPES
                oss << "32";
#endif
            }
            break;
        case 64:
            if (sm >= 60) {
                if (type.is_uint()) {
                    oss << "uint64_t";
                } else {
                    oss << "int64_t";
                }
            } else {
                user_error << "HLSL SM 5.1 does not support 64-bit integers. "
                           << "Use target feature d3d12compute_sm60 or higher.\n";
            }
            break;
        default:
            user_error << "Can't represent an integer with this many bits in HLSL: " << type << "\n";
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
    default:
        if (type.lanes() >= 5 && type.lanes() <= 1024 && sm >= 69) {
            // SM 6.9+ long vector: rewrite "TYPE" as "vector<TYPE, N>".
            string scalar = oss.str();
            oss.str("");
            oss.clear();
            oss << "vector<" << scalar << ", " << type.lanes() << ">";
        } else {
            user_error << "Unsupported vector width in HLSL: " << type << ". "
                       << "Vectors wider than 4 elements require d3d12compute_sm69.\n";
        }
        break;
    }

    if (space == AppendSpace) {
        oss << " ";
    }

    return oss.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_type(Type type, AppendSpaceIfNeeded space) {
    return print_type_maybe_storage(type, false, space);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_storage_type(Type type) {
    return print_type_maybe_storage(type, true, DoNotAppendSpace);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinterpret(Type type, const Expr &e) {
    if (type == e.type()) {
        return print_expr(e);
    } else {
        return print_reinterpret_cast(type, print_expr(e));
    }
}

namespace {
string simt_intrinsic(const string &name) {
    if (ends_with(name, gpu_thread_name(0))) {
        return "tid_in_tgroup.x";
    } else if (ends_with(name, gpu_thread_name(1))) {
        return "tid_in_tgroup.y";
    } else if (ends_with(name, gpu_thread_name(2))) {
        return "tid_in_tgroup.z";
    } else if (ends_with(name, gpu_block_name(0))) {
        return "tgroup_index.x";
    } else if (ends_with(name, gpu_block_name(1))) {
        return "tgroup_index.y";
    } else if (ends_with(name, gpu_block_name(2))) {
        return "tgroup_index.z";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return "";
}
}  // namespace

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Evaluate *op) {
    if (is_const(op->value)) {
        return;
    }
    print_expr(op->value);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Div *op) {
    if (auto bits = is_const_power_of_two_integer(op->b)) {
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << *bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Mod *op) {
    if (auto bits = is_const_power_of_two_integer(op->b)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << (((uint64_t)1 << *bits) - 1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const For *loop) {
    user_assert(loop->for_type != ForType::GPULane)
        << "The D3D12Compute backend does not support the gpu_lanes() scheduling directive.";

    if (!is_gpu(loop->for_type)) {
        CodeGen_GPU_C::visit(loop);
        return;
    }
    internal_assert(is_const_zero(loop->min));

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
        if (i < op->lanes - 1) {
            rhs << ", ";
        }
    }
    rhs << ")";

    print_assignment(op->type.with_lanes(op->lanes), rhs.str());
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Call *op) {
    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        auto fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        // By default, we'll just issue a Group (aka Shared) memory barrier,
        // since it seems there isn't a sync without a memory barrier
        // available
        // NOTE(shoaibkamil): should we replace with AllMemoryBarrierWithGroupSync()
        // if both are required?
        if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device &&
            !(fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared)) {
            stream << get_indent() << "DeviceMemoryBarrierWithGroupSync();\n";
        } else if (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device) {
            stream << get_indent() << "DeviceMemoryBarrier();\n";
        }
        stream << get_indent() << "GroupMemoryBarrierWithGroupSync();\n";
        print_assignment(op->type, "0");
    } else if (op->is_intrinsic(Call::image_load)) {
        // image_load(<name>, <buffer>, <x>, <x-extent>, [<y>, <y-extent>, [<z>, <z-extent>]])
        // dims = (args.size() - 2) / 2
        int dims = ((int)op->args.size() - 2) / 2;
        internal_assert(dims >= 1 && dims <= 3);

        const StringImm *name_imm = op->args[0].as<StringImm>();
        if (!name_imm) {
            // vectorized access — use the value from Broadcast
            internal_assert(op->args[0].as<Broadcast>());
            name_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(name_imm);

        // Collect per-dimension coordinate expressions (args[2], args[4], args[6])
        vector<string> coords;
        coords.reserve(dims);
        for (int i = 0; i < dims; i++) {
            coords.push_back(print_expr(op->args[i * 2 + 2]));
        }

        ostringstream rhs;
        rhs << print_name(name_imm->value) << "[";
        if (dims == 1) {
            rhs << coords[0];
        } else if (dims == 2) {
            rhs << "uint2(" << coords[0] << ", " << coords[1] << ")";
        } else {
            rhs << "uint3(" << coords[0] << ", " << coords[1] << ", " << coords[2] << ")";
        }
        rhs << "]";

        print_assignment(op->type, rhs.str());
    } else if (op->is_intrinsic(Call::image_store)) {
        // image_store(<name>, <buffer>, <x>, [<y>, [<z>,]] <value>)
        // dims = args.size() - 3  (name, buffer, value are fixed; rest are coords)
        int dims = (int)op->args.size() - 3;
        internal_assert(dims >= 1 && dims <= 3);

        const StringImm *name_imm = op->args[0].as<StringImm>();
        if (!name_imm) {
            internal_assert(op->args[0].as<Broadcast>());
            name_imm = op->args[0].as<Broadcast>()->value.as<StringImm>();
        }
        internal_assert(name_imm);

        // Coords are args[2..2+dims-1], value is args.back()
        vector<string> coords;
        coords.reserve(dims);
        for (int i = 0; i < dims; i++) {
            coords.push_back(print_expr(op->args[i + 2]));
        }
        string value = print_expr(op->args.back());

        stream << get_indent() << print_name(name_imm->value) << "[";
        if (dims == 1) {
            stream << coords[0];
        } else if (dims == 2) {
            stream << "uint2(" << coords[0] << ", " << coords[1] << ")";
        } else {
            stream << "uint3(" << coords[0] << ", " << coords[1] << ", " << coords[2] << ")";
        }
        stream << "] = " << value << ";\n";
    } else if (op->name == "pow_f32" && can_prove(op->args[0] > 0)) {
        // If we know pow(x, y) is called with x > 0, we can use HLSL's pow
        // directly.
        Expr equiv = Call::make(op->type, "pow", op->args, Call::PureExtern);
        equiv.accept(this);
    } else if (op->is_intrinsic(Call::round)) {
        // HLSL's round intrinsic has the correct semantics for our rounding.
        Expr equiv = Call::make(op->type, "round", op->args, Call::PureExtern);
        equiv.accept(this);
    } else {
        CodeGen_GPU_C::visit(op);
    }
}

namespace {

// If e is a ramp expression with stride 1, return the base, otherwise undefined.
Expr is_ramp_one(const Expr &e) {
    const Ramp *r = e.as<Ramp>();
    if (r == nullptr) {
        return Expr();
    }

    if (is_const_one(r->stride)) {
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

struct StoragePackUnpack {
    using CodeGen = CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C;

    // Shader Model 5.1: threadgroup shared memory is limited 32KB
    static const size_t ThreadGroupSharedStorageLimit = 32 * 1024;

    void pack_storage(const Allocate *op, size_t elements, size_t size_in_bytes) {
        // we could try to compact things for smaller types:
        size_t packing_factor = 1;
        while (size_in_bytes > ThreadGroupSharedStorageLimit) {
            // must pack/unpack elements to/from shared memory...
            elements = (elements + 1) & ~size_t(1);
            elements /= 2;
            size_in_bytes = (size_in_bytes + 1) & ~size_t(1);
            size_in_bytes /= 2;
            packing_factor *= 2;
        }
        // smallest possible pack type is a byte (no nibbles)
        internal_assert(packing_factor <= 4);
    }

    std::ostringstream pack_store(CodeGen &cg, const Store *op) {
        std::ostringstream lhs;
        // NOTE(marcos): 8bit and 16bit word packing -- the smallest integer
        // type granularity available in HLSL SM 5.1 is 32bit (int/uint):
        Type value_type = op->value.type();
        if (value_type.bits() == 32) {
            // storing a 32bit word? great! just reinterpret value to uint32:
            lhs << cg.print_name(op->name)
                << "[" << cg.print_expr(op->index) << "]"
                << " = "
                << cg.print_reinterpret(UInt(32), op->value)
                << ";\n";
        } else {
            // nightmare:
            internal_assert(value_type.bits() < 32);
            // must map element index to uint word array index:
            ostringstream index;
            auto bits = value_type.bits();
            auto divisor = (32 / bits);
            auto i = cg.print_expr(op->index);
            index << i << " / " << divisor;
            ostringstream word;
            word << cg.print_name(op->name)
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
                  << "(" << cg.print_expr(op->value) << ")"
                  << ")";

            // the performance impact of atomic operations on shared memory is
            // not well documented... here is something:
            // https://stackoverflow.com/a/19548723
            lhs << cg.get_indent() << "InterlockedAnd(" << word.str() << ", "
                << "~" << mask.str() << ");\n";
            lhs << cg.get_indent() << "InterlockedXor(" << word.str() << ", " << value.str() << ");\n";
        }
        return lhs;
    }

    std::ostringstream unpack_load(CodeGen &cg, const Load *op) {
        std::ostringstream rhs;
        // NOTE(marcos): let's keep this block of code here (disabled) in case
        // we need to "emulate" byte/short packing in shared memory (recall that
        // the smallest type granularity in HLSL SM 5.1 allows is 32bit types):
        if (op->type.bits() == 32) {
            // loading a 32bit word? great! just reinterpret as float/int/uint
            rhs << "as" << cg.print_type(op->type.element_of())
                << "("
                << cg.print_name(op->name)
                << "[" << cg.print_expr(op->index) << "]"
                << ")";
        } else {
            // not a 32bit word? hell ensues:
            internal_assert(op->type.bits() < 32);
            // must map element index to uint word array index:
            ostringstream index;
            auto bits = op->type.bits();
            auto divisor = (32 / bits);
            auto i = cg.print_expr(op->index);
            index << i << " / " << divisor;
            ostringstream word;
            word << cg.print_name(op->name)
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
        return rhs;
    }
};

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Load *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated load is not supported inside D3D12Compute kernel.\n";

    // elements in a threadgroup shared buffer are always 32bits:
    // must reinterpret (and maybe unpack) bits.
    bool shared_promotion_required = false;
    string promotion_str = "";
    if (groupshared_allocations.contains(op->name)) {
        internal_assert(allocations.contains(op->name));
        Type promoted_type = op->type.with_bits(32).with_lanes(1);
        if (promoted_type != op->type) {
            shared_promotion_required = true;
            // NOTE(marcos): might need to resort to StoragePackUnpack::unpack_load() here
            promotion_str = "as" + print_type(promoted_type);
        }
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
            if (shared_promotion_required) {
                rhs << promotion_str << "(";
            }
            rhs << print_name(op->name)
                << "["
                << print_expr(ramp_base)
                << "+" << i
                << "]";
            if (shared_promotion_required) {
                rhs << ")";
            }
            if (i < lanes - 1) {
                rhs << ", ";
            }
        }
        rhs << ")";

        print_assignment(op->type, rhs.str());

        return;
    }

    string id_index = print_expr(op->index);

    // Get the rhs just for the cache.
    const auto *alloc = allocations.find(op->name);
    bool type_cast_needed = !(alloc &&
                              alloc->type == op->type);

    ostringstream rhs;
    if (type_cast_needed) {
        ostringstream element;
        // Vector cases handled below
        if (shared_promotion_required && !op->index.type().is_vector()) {
            element << promotion_str << "(";
        }
        element << print_name(op->name)
                << "[" << id_index << "]";
        if (shared_promotion_required && !op->index.type().is_vector()) {
            element << ")";
        }
        Type target_type = op->type;
        Type source_type = allocations.get(op->name).type;
        rhs << print_cast(target_type, source_type, element.str());
    } else {
        // Vector cases handled below
        if (shared_promotion_required && !op->index.type().is_vector()) {
            rhs << promotion_str << "(";
        }
        rhs << print_name(op->name);
        rhs << "[" << id_index << "]";
        if (shared_promotion_required && !op->index.type().is_vector()) {
            rhs << ")";
        }
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
                   << "(";
            if (shared_promotion_required) {
                stream << promotion_str << "(";
            }
            stream << print_name(op->name)
                   << "[" << id_index << "[" << i << "]]";
            if (shared_promotion_required) {
                stream << ")";
            }
            stream << ");\n";
        }
    } else {
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Store *op) {
    user_assert(is_const_one(op->predicate)) << "Predicated store is not supported inside D3D12Compute kernel.\n";

    if (emit_atomic_stores) {
        // Atomic stores: scalar only; vectorized atomics are not supported in HLSL.
        user_assert(op->value.type().is_scalar())
            << "D3D12Compute does not support vectorized atomic stores.\n";

        const int sm = target.get_d3d12compute_capability_lower_bound();
        Type t = op->value.type();
        string id_index = print_expr(op->index);
        string dest = print_name(op->name) + "[" + id_index + "]";

        // Detect atomic add: value = load + delta where delta is independent of the buffer.
        Expr equiv_load = Load::make(t, op->name, op->index, Buffer<>(),
                                     op->param, op->predicate, op->alignment);
        Expr delta = simplify(common_subexpression_elimination(op->value - equiv_load));
        bool is_atomic_add = !expr_uses_var(delta, op->name);

        if (is_atomic_add && t.is_int_or_uint()) {
            // InterlockedAdd works on int/uint in UAV buffers and groupshared.
            // Pre-evaluate delta so any temporaries are emitted before the call.
            string delta_str = print_expr(delta);
            stream << get_indent() << "InterlockedAdd(" << dest << ", " << delta_str << ");\n";
        } else if (t.is_float()) {
            // Float atomics: only add is supported, and only for SM 6.6+.
            if (is_atomic_add && t.bits() == 32 && sm >= 66) {
                // SM 6.6+: float32 atomic add via CAS loop.
                // DXC 1.8 does not expose InterlockedAddF32 for typed UAV resources;
                // use InterlockedCompareExchangeFloatBitwise instead.
                string delta_str = print_expr(delta);
                stream << get_indent() << "{\n";
                stream << get_indent() << "    float _hfatomic_old, _hfatomic_orig;\n";
                stream << get_indent() << "    [allow_uav_condition]\n";
                stream << get_indent() << "    do {\n";
                stream << get_indent() << "        _hfatomic_old = " << dest << ";\n";
                stream << get_indent() << "        InterlockedCompareExchangeFloatBitwise("
                       << dest << ", _hfatomic_old, _hfatomic_old + " << delta_str
                       << ", _hfatomic_orig);\n";
                stream << get_indent() << "    } while (asuint(_hfatomic_orig) != asuint(_hfatomic_old));\n";
                stream << get_indent() << "}\n";
            } else if (is_atomic_add && t.bits() == 64 && sm >= 66) {
                // SM 6.6+: float64 atomic add via CAS loop.
                string delta_str = print_expr(delta);
                stream << get_indent() << "{\n";
                stream << get_indent() << "    double _hdatomic_old, _hdatomic_orig;\n";
                stream << get_indent() << "    [allow_uav_condition]\n";
                stream << get_indent() << "    do {\n";
                stream << get_indent() << "        _hdatomic_old = " << dest << ";\n";
                stream << get_indent() << "        InterlockedCompareExchangeFloatBitwise("
                       << dest << ", _hdatomic_old, _hdatomic_old + " << delta_str
                       << ", _hdatomic_orig);\n";
                stream << get_indent() << "    } while (asuint64(_hdatomic_orig) != asuint64(_hdatomic_old));\n";
                stream << get_indent() << "}\n";
            } else if (is_atomic_add) {
                user_assert(false)
                    << "D3D12Compute: float atomic add requires SM 6.6+ "
                    << "(add d3d12compute_sm66 to your target features). "
                    << "Type was: " << t << "\n";
            } else {
                user_assert(false)
                    << "D3D12Compute: only atomic add is supported for float types in HLSL "
                    << "(SM 6.6+ for float32/float64). Type was: " << t << "\n";
            }
            cache.clear();
            return;
        } else if (t.is_int_or_uint()) {
            // Detect min/max/bitwise patterns by examining the expression structure.
            // Each has the form op(load, rhs) where rhs is independent of the buffer.
            auto detect_rhs = [&](const Expr &a, const Expr &b) -> Expr {
                if (equal(a, equiv_load) && !expr_uses_var(b, op->name)) {
                    return b;
                }
                if (equal(b, equiv_load) && !expr_uses_var(a, op->name)) {
                    return a;
                }
                return Expr();
            };
            Expr rhs;
            if (const Min *mn = op->value.as<Min>()) {
                if ((rhs = detect_rhs(mn->a, mn->b)).defined()) {
                    stream << get_indent() << "InterlockedMin(" << dest << ", "
                           << print_expr(rhs) << ");\n";
                    cache.clear();
                    return;
                }
            }
            if (const Max *mx = op->value.as<Max>()) {
                if ((rhs = detect_rhs(mx->a, mx->b)).defined()) {
                    stream << get_indent() << "InterlockedMax(" << dest << ", "
                           << print_expr(rhs) << ");\n";
                    cache.clear();
                    return;
                }
            }
            if (const Call *c = op->value.as<Call>()) {
                if (c->is_intrinsic(Call::bitwise_and) && c->args.size() == 2) {
                    if ((rhs = detect_rhs(c->args[0], c->args[1])).defined()) {
                        stream << get_indent() << "InterlockedAnd(" << dest << ", "
                               << print_expr(rhs) << ");\n";
                        cache.clear();
                        return;
                    }
                }
                if (c->is_intrinsic(Call::bitwise_or) && c->args.size() == 2) {
                    if ((rhs = detect_rhs(c->args[0], c->args[1])).defined()) {
                        stream << get_indent() << "InterlockedOr(" << dest << ", "
                               << print_expr(rhs) << ");\n";
                        cache.clear();
                        return;
                    }
                }
                if (c->is_intrinsic(Call::bitwise_xor) && c->args.size() == 2) {
                    if ((rhs = detect_rhs(c->args[0], c->args[1])).defined()) {
                        stream << get_indent() << "InterlockedXor(" << dest << ", "
                               << print_expr(rhs) << ");\n";
                        cache.clear();
                        return;
                    }
                }
            }
            // Generic integer atomic: use InterlockedExchange for plain assignment,
            // or fall back to a compare-and-swap loop for other patterns.
            if (!expr_uses_var(op->value, op->name)) {
                // Value is independent of current buffer contents: plain atomic exchange.
                stream << get_indent() << "InterlockedExchange(" << dest << ", "
                       << print_expr(op->value) << ", " << unique_name("_halide_atom_old") << ");\n";
            } else {
                // CAS loop for unrecognised integer RMW.
                string old_var = unique_name("_halide_atom_old");
                string new_var = unique_name("_halide_atom_new");
                stream << get_indent() << "{\n";
                indent += 2;
                stream << get_indent() << print_type(t) << " " << old_var << ", " << new_var << ";\n";
                stream << get_indent() << "[loop] do {\n";
                indent += 2;
                stream << get_indent() << old_var << " = " << dest << ";\n";
                cache.clear();
                stream << get_indent() << new_var << " = " << print_expr(op->value) << ";\n";
                stream << get_indent() << "InterlockedCompareExchange("
                       << dest << ", " << old_var << ", " << new_var << ", " << old_var << ");\n";
                indent -= 2;
                stream << get_indent() << "} while (" << old_var << " != " << new_var << ");\n";
                indent -= 2;
                stream << get_indent() << "}\n";
            }
        } else {
            user_assert(false)
                << "D3D12Compute: unsupported type for atomic operation: " << t << "\n";
        }
        cache.clear();
        return;
    }

    Type value_type = op->value.type();

    // elements in a threadgroup shared buffer are always 32bits:
    // must reinterpret (and maybe pack) bits.
    bool shared_promotion_required = false;
    string promotion_str = "";
    if (groupshared_allocations.contains(op->name)) {
        const auto *alloc = allocations.find(op->name);
        internal_assert(alloc);
        Type promoted_type = alloc->type;
        if (promoted_type != op->value.type()) {
            shared_promotion_required = true;
            // NOTE(marcos): might need to resort to StoragePackUnpack::pack_store() here
            promotion_str = "as" + print_type(promoted_type);
        }
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
                << " = ";
            if (shared_promotion_required) {
                rhs << promotion_str << "(";
            }
            rhs << print_expr(op->value)
                << "["
                << i
                << "]";
            if (shared_promotion_required) {
                rhs << ")";
            }
            rhs << ";\n";
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
                << " = ";
            if (shared_promotion_required) {
                rhs << promotion_str << "(";
            }
            rhs << print_expr(op->value) << "[" << i << "]";
            if (shared_promotion_required) {
                rhs << ")";
            }
            rhs << ";\n";
            stream << get_indent() << rhs.str();
        }
    } else {
        ostringstream rhs;
        rhs << print_name(op->name)
            << "[" << print_expr(op->index) << "]"
            << " = ";
        if (shared_promotion_required) {
            rhs << promotion_str << "(";
        }
        rhs << print_expr(op->value);
        if (shared_promotion_required) {
            rhs << ")";
        }
        rhs << ";\n";
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

bool is_shared_allocation(const Allocate *op) {
    return op->memory_type == MemoryType::GPUShared;
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Allocate *op) {

    if (is_shared_allocation(op)) {
        // Already handled
        internal_assert(!groupshared_allocations.contains(op->name));
        groupshared_allocations.push(op->name);
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

        stream << get_indent() << print_storage_type(op->type) << " "
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
    if (groupshared_allocations.contains(op->name)) {
        groupshared_allocations.pop(op->name);
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
    return CodeGen_GPU_C::print_assignment(type, rhs_modified);
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_vanilla_cast(Type type, const string &value_expr) {
    ostringstream ss;
    ss << print_type(type) << "(" << value_expr << ")";
    return ss.str();
}

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinforced_cast(Type type, const string &value_expr) {
    if (type.is_float() || type.is_bool() || type.bits() >= 32) {
        return value_expr;
    }

    // SM 6.2+ has native 16-bit types; asint/asuint don't accept int16_t/uint16_t.
    const int sm = target.get_d3d12compute_capability_lower_bound();
    if (sm >= 62 && type.bits() == 16) {
        return value_expr;
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

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_reinterpret_cast(Type type, const string &value_expr) {
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

string CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::print_cast(Type target_type, Type source_type, const string &value_expr) {
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

    // SM 6.0+ supports native 64-bit integers; use a plain cast.
    // SM 6.2+ supports native 16-bit types; the sub-32-bit emulation below
    // uses asint/asuint which do not accept int16_t/uint16_t in DXC.
    {
        const int sm = target.get_d3d12compute_capability_lower_bound();
        if (target_type.bits() == 64 || source_type.bits() == 64) {
            return print_vanilla_cast(target_type, value_expr);
        }
        if (sm >= 62 && (target_type.bits() == 16 || source_type.bits() == 16)) {
            return print_vanilla_cast(target_type, value_expr);
        }
    }

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

    string cast_expr = print_cast(target_type, source_type, print_expr(op->value));

    print_assignment(target_type, cast_expr);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const AssertStmt *op) {
    user_warning << "Ignoring assertion inside D3D12Compute kernel: " << op->condition << "\n";
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Shuffle *op) {
    if (op->is_interleave()) {
        int op_lanes = op->type.lanes();
        int num_vectors = (int)op->vectors.size();
        int arg_lanes = op->vectors[0].type().lanes();
        if (num_vectors == 1) {
            print_assignment(op->type, print_expr(op->vectors[0]));
            return;
        }
        vector<string> arg_exprs(num_vectors);
        for (int i = 0; i < num_vectors; i++) {
            arg_exprs[i] = print_expr(op->vectors[i]);
        }
        ostringstream rhs;
        rhs << print_type(op->type) << "(";
        for (int i = 0; i < op_lanes; i++) {
            int src = i % num_vectors;
            int lane = i / num_vectors;
            rhs << arg_exprs[src];
            if (arg_lanes > 1) {
                rhs << "[" << lane << "]";
            }
            if (i < op_lanes - 1) {
                rhs << ", ";
            }
        }
        rhs << ")";
        print_assignment(op->type, rhs.str());
    } else if (op->is_extract_element()) {
        // Extract a single scalar from a vector using subscript.
        ostringstream rhs;
        rhs << print_expr(op->vectors[0]) << "[" << op->indices[0] << "]";
        print_assignment(op->type, rhs.str());
    } else {
        // Generic shuffle or slice: build output vector element by element.
        int total_src_lanes = 0;
        vector<string> src_exprs;
        vector<int> src_offsets;
        for (const auto &v : op->vectors) {
            src_offsets.push_back(total_src_lanes);
            src_exprs.push_back(print_expr(v));
            total_src_lanes += v.type().lanes();
        }
        int out_lanes = op->type.lanes();
        ostringstream rhs;
        if (out_lanes > 1) {
            rhs << print_type(op->type) << "(";
        }
        for (int i = 0; i < out_lanes; i++) {
            int idx = op->indices[i];
            // Find source vector for this index
            int src = (int)src_exprs.size() - 1;
            for (int s = 0; s + 1 < (int)op->vectors.size(); s++) {
                if (idx < src_offsets[s + 1]) {
                    src = s;
                    break;
                }
            }
            int lane = idx - src_offsets[src];
            rhs << src_exprs[src];
            if (op->vectors[src].type().lanes() > 1) {
                rhs << "[" << lane << "]";
            }
            if (i < out_lanes - 1) {
                rhs << ", ";
            }
        }
        if (out_lanes > 1) {
            rhs << ")";
        }
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const Atomic *op) {
    user_assert(op->mutex_name.empty())
        << "D3D12Compute does not support atomic operations that require a mutex lock.\n";
    ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
    CodeGen_GPU_C::visit(op);
}

void CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C::visit(const FloatImm *op) {
    // For SM 6.2+ native float16_t, use asfloat16() with the exact uint16 bit
    // pattern to avoid a DXC -Wconversion warning (float->float16_t narrowing).
    // float_from_bits is defined as asfloat() which returns float32; when the
    // result is assigned to a float16_t variable DXC warns about the truncation.
    const int sm = target.get_d3d12compute_capability_lower_bound();
    if (sm >= 62 && op->type.bits() == 16) {
        const uint16_t bits = float16_t(op->value).to_bits();
        ostringstream oss;
        oss << "asfloat16((uint16_t)" << bits << "u /* " << op->value << " */)";
        print_assignment(op->type, oss.str());
        return;
    }
    CodeGen_GPU_C::visit(op);
}

void CodeGen_D3D12Compute_Dev::add_kernel(Stmt s,
                                          const string &name,
                                          const vector<DeviceArgument> &args) {
    debug(2) << "CodeGen_D3D12Compute_Dev::compile " << name << "\n";

    // We need to scalarize/de-predicate any loads/stores, since HLSL does not
    // support predication.
    s = scalarize_predicated_loads_stores(s);

    debug(2) << "CodeGen_D3D12Compute_Dev: after removing predication: \n"
             << s;

    // TODO: do we have to uniquify these names, or can we trust that they are safe?
    cur_kernel_name = name;
    d3d12compute_c.add_kernel(s, name, args);
}

namespace {
struct BufferSize {
    string name;
    size_t size = 0;

    BufferSize() = default;
    BufferSize(string name, size_t size)
        : name(std::move(name)), size(size) {
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

    // HLSL has no bool vector type; lower bool vectors to integer masks.
    s = eliminate_bool_vectors(s);

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
    for (const auto &arg : args) {
        if (isConstantBuffer(arg)) {
            constants.emplace_back(arg.name, arg.size);
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

    // Find all the shared allocations, uniquify their names,
    // and declare them at global scope.
    class FindSharedAllocationsAndUniquify : public IRMutator {
        using IRMutator::visit;
        Stmt visit(const Allocate *op) override {
            if (is_shared_allocation(op)) {
                // Because these will go in global scope,
                // we need to ensure they have unique names.
                std::string new_name = unique_name(op->name);
                replacements[op->name] = new_name;

                auto new_extents = mutate(op->extents);
                Stmt new_body = mutate(op->body);
                Expr new_condition = mutate(op->condition);
                Expr new_new_expr;
                if (op->new_expr.defined()) {
                    new_new_expr = mutate(op->new_expr);
                }

                Stmt new_alloc = Allocate::make(new_name, op->type, op->memory_type, new_extents,
                                                std::move(new_condition), std::move(new_body),
                                                std::move(new_new_expr), op->free_function, op->padding);

                allocs.push_back(new_alloc);
                replacements.erase(op->name);
                return new_alloc;
            } else {
                return IRMutator::visit(op);
            }
        }

        Stmt visit(const Free *op) override {
            auto it = replacements.find(op->name);
            if (it != replacements.end()) {
                return Free::make(it->second);
            } else {
                return IRMutator::visit(op);
            }
        }

        Expr visit(const Load *op) override {
            auto it = replacements.find(op->name);
            if (it != replacements.end()) {
                return Load::make(op->type, it->second,
                                  mutate(op->index), op->image, op->param,
                                  mutate(op->predicate), op->alignment);
            } else {
                return IRMutator::visit(op);
            }
        }

        Stmt visit(const Store *op) override {
            auto it = replacements.find(op->name);
            if (it != replacements.end()) {
                return Store::make(it->second, mutate(op->value),
                                   mutate(op->index), op->param,
                                   mutate(op->predicate), op->alignment);
            } else {
                return IRMutator::visit(op);
            }
        }

        std::map<string, string> replacements;
        friend class CodeGen_D3D12Compute_Dev::CodeGen_D3D12Compute_C;
        vector<Stmt> allocs;
    };

    FindSharedAllocationsAndUniquify fsa;
    s = fsa(s);

    uint32_t total_shared_bytes = 0;
    for (const Stmt &sop : fsa.allocs) {
        const Allocate *op = sop.as<Allocate>();
        internal_assert(op->extents.size() == 1);
        internal_assert(op->type.lanes() == 1);
        // In D3D12/HLSL, only 32bit types (int/uint/float) are supported (even
        // though things are changing with newer shader models). Since there is
        // no uint8 type, we'll have to emulate it with 32bit types...
        // This will also require pack/unpack logic with bit-masking and aliased
        // type reinterpretation via asfloat()/asuint() in the shader code... :(
        Type smem_type = op->type;
        smem_type.with_bits(32);
        stream << "groupshared"
               << " " << print_type(smem_type)
               << " " << print_name(op->name);
        if (is_const(op->extents[0])) {
            std::stringstream ss;
            ss << op->extents[0];
            size_t elements = 0;
            ss >> elements;
            size_t bytesize = elements * sizeof(uint32_t);
            // NOTE(marcos): might need to resort to StoragePackUnpack::pack_storage() here...
            internal_assert(bytesize <= StoragePackUnpack::ThreadGroupSharedStorageLimit);
            total_shared_bytes += bytesize;
            stream << " [" << elements << "];\n";
        } else {
            // fill-in __GROUPSHARED_SIZE_IN_BYTES later on when D3DCompile()
            // is invoked in halide_d3d12compute_run(); must get divided by 4
            // since groupshared memory elements have 32bit granularity:
            stream << " [ ( __GROUPSHARED_SIZE_IN_BYTES + 3 ) / 4 ];\n";
        }
        if (total_shared_bytes > StoragePackUnpack::ThreadGroupSharedStorageLimit) {
            debug(1) << "D3D12 CodeGen ERROR: Total thread group shared memory required for kernel '" << name
                     << "' exceeds the SM 5.1 limit of 32KB: " << total_shared_bytes << " bytes required.\n";
        }
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
    }

    // Emit the kernel function preamble (numtreads):
    // must first figure out the thread group size by traversing the stmt:
    struct FindThreadGroupSize : public IRVisitor {
        using IRVisitor::visit;
        void visit(const For *loop) override {
            if (!is_gpu(loop->for_type)) {
                loop->body.accept(this);
                return;
            }
            if (loop->for_type != ForType::GPUThread) {
                loop->body.accept(this);
                return;
            }
            internal_assert(is_const_zero(loop->min));
            int index = thread_loop_workgroup_index(loop->name);
            user_assert(index >= 0) << "Invalid 'numthreads' index for loop variable '" << loop->name << "'.\n";
            // if 'numthreads' for a given dimension can't be determined at code
            // generation time, emit code such that it can be patched some point
            // later when calling D3DCompile() / halide_d3d12compute_run()
            numthreads[index] = 0;  // <-- 0 indicates 'undetermined'
            Expr extent = simplify(loop->extent());
            const IntImm *int_limit = extent.as<IntImm>();
            if (nullptr != int_limit) {
                numthreads[index] = int_limit->value;
                user_assert(numthreads[index] > 0) << "For D3D12Compute, 'numthreads[" << index << "]' values must be greater than zero.\n";
            }
            debug(4) << "Thread group size for index " << index << " is " << numthreads[index] << "\n";
            loop->body.accept(this);
        }
        int thread_loop_workgroup_index(const string &name) {
            for (int i = 0; i < 3; i++) {
                if (ends_with(name, gpu_thread_name(i))) {
                    return i;
                }
            }
            return -1;
        }
        int numthreads[3] = {1, 1, 1};
    };
    FindThreadGroupSize ftg;
    s.accept(&ftg);
    const int sm = target.get_d3d12compute_capability_lower_bound();
    const bool use_dxc = (sm >= 60);

    if (use_dxc) {
        // DXC (SM 6.x) does not support FXC-style resource/uniform parameters to entry
        // functions. Declare all resources globally with explicit register bindings and
        // put scalar uniforms in a constant buffer. The runtime binds:
        //   - scalar args → cbuffer at register(b0)
        //   - buffer args → UAV at register(u0), register(u1), ...
        int uav_index = 0;
        bool has_scalars = false;
        for (const auto &arg : args) {
            if (!arg.is_buffer) {
                has_scalars = true;
                continue;
            }
            if (arg.memory_type == MemoryType::GPUTexture) {
                int dims = arg.dimensions;
                internal_assert(dims >= 1 && dims <= 3) << "D3D12Compute texture must have 1-3 dimensions\n";
                stream << "RWTexture" << dims << "D"
                       << "<" << print_type(arg.type) << ">"
                       << " " << print_name(arg.name)
                       << " : register(u" << uav_index++ << ");\n";
            } else {
                stream << "RWBuffer"
                       << "<" << print_type(arg.type) << ">"
                       << " " << print_name(arg.name)
                       << " : register(u" << uav_index++ << ");\n";
            }
            Allocation alloc;
            alloc.type = arg.type;
            allocations.push(arg.name, alloc);
        }
        if (has_scalars) {
            stream << "cbuffer _halide_uniform_args : register(b0) {\n";
            for (const auto &arg : args) {
                if (!arg.is_buffer) {
                    stream << " " << print_type(arg.type) << " " << print_name(arg.name) << ";\n";
                }
            }
            stream << "};\n";
        }
    }

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
    stream << " "
           << "uint3 tgroup_index  : SV_GroupID,\n"
           << " "
           << "uint3 tid_in_tgroup : SV_GroupThreadID";
    if (!use_dxc) {
        for (const auto &arg : args) {
            stream << ",\n";
            stream << " ";
            if (arg.is_buffer) {
                if (arg.memory_type == MemoryType::GPUTexture) {
                    int dims = arg.dimensions;
                    internal_assert(dims >= 1 && dims <= 3) << "D3D12Compute texture must have 1-3 dimensions\n";
                    stream << "RWTexture" << dims << "D"
                           << "<" << print_type(arg.type) << ">"
                           << " " << print_name(arg.name);
                } else {
                    // NOTE(marcos): Passing all buffers as RWBuffers in order to bind
                    // all buffers as UAVs since there is no way the runtime can know
                    // if a given halide_buffer_t is read-only (SRV) or read-write...
                    stream << "RWBuffer"
                           << "<" << print_type(arg.type) << ">"
                           << " " << print_name(arg.name);
                }
                Allocation alloc;
                alloc.type = arg.type;
                allocations.push(arg.name, alloc);
            } else {
                stream << "uniform"
                       << " " << print_type(arg.type)
                       << " " << print_name(arg.name);
            }
        }
    }
    stream << ")\n";

    // Emit the kernel code:

    open_scope();
    print(s);
    close_scope("kernel " + name);

    for (const auto &arg : args) {
        // Remove buffer arguments from allocation scope
        if (arg.is_buffer) {
            allocations.pop(arg.name);
        }
    }

    stream << "\n";
}

void CodeGen_D3D12Compute_Dev::init_module() {
    debug(2) << "D3D12Compute device codegen init_module\n";

    // TODO: we could support strict float intrinsics with the precise qualifier
    internal_assert(!any_strict_float)
        << "strict float intrinsics not yet supported in d3d12compute backend";

    // wipe the internal kernel source
    src_stream.str("");
    src_stream.clear();

    // Emit SM version marker for runtime to select FXC (SM 5.1) vs DXC (SM 6.x)
    int sm = d3d12compute_c.get_target().get_d3d12compute_capability_lower_bound();
    if (sm >= 60) {
        src_stream << "//HALIDE_D3D12_SM " << sm << "\n";
    }

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

    src_stream << "#define halide_maybe_unused(x) (void)(x)\n";

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
        // DXC (SM 6.x) rejects FXC-specific float literals 1.#IND / 1.#INF.
        // Use asfloat() with IEEE 754 bit patterns instead.
        << (sm >= 60 ? "float nan_f32()     { return asfloat(0x7fc00000u); } \n"
                       "float neg_inf_f32() { return asfloat(0xff800000u); } \n"
                       "float inf_f32()     { return asfloat(0x7f800000u); } \n" :
                       "float nan_f32()     { return  1.#IND; } \n"
                       "float neg_inf_f32() { return -1.#INF; } \n"
                       "float inf_f32()     { return +1.#INF; } \n")
        << "#define float_from_bits asfloat \n"
        // pow() in HLSL has the same semantics as C if
        // x > 0.  Otherwise, we need to emulate C
        // behavior.
        // TODO(shoaibkamil): Can we simplify this?
        << "float pow_f32(float x, float y) { \n"
        << "  if (x > 0.0) {                  \n"
        << "    return pow(x, y);             \n"
        << "  } else if (y == 0.0) {          \n"
        << "    return 1.0f;                  \n"
        << "  } else if (trunc(y) == y) {     \n"
        << "    if (fmod(y, 2) == 0) {        \n"
        << "      return pow(abs(x), y);      \n"
        << "    } else {                      \n"
        << "      return -pow(abs(x), y);     \n"
        << "    }                             \n"
        << "  } else {                        \n"
        << "    return nan_f32();             \n"
        << "  }                               \n"
        << "}                                 \n"
        << "#define asinh(x) (log(x + sqrt(x*x + 1))) \n"
        << "#define acosh(x) (log(x + sqrt(x*x - 1))) \n"
        << "#define atanh(x) (log((1+x)/(1-x))/2) \n"
        << "\n";
    //<< "}\n"; // close namespace

    src_stream << "\n";

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
    std::cerr << src_stream.str() << "\n";
}

std::string CodeGen_D3D12Compute_Dev::print_gpu_name(const std::string &name) {
    return name;
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_D3D12Compute_Dev(const Target &target) {
    return std::make_unique<CodeGen_D3D12Compute_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide

#include <algorithm>
#include <fstream>  // for dump to file
#include <sstream>
#include <unordered_set>

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Vulkan_Dev.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "FindIntrinsics.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "SpirvIR.h"
#include "Target.h"

#ifdef WITH_SPIRV

namespace Halide {
namespace Internal {

class CodeGen_LLVM;

namespace {  // anonymous

// --

template<typename CodeGenT, typename ValueT>
ValueT lower_int_uint_div(CodeGenT *cg, Expr a, Expr b);

template<typename CodeGenT, typename ValueT>
ValueT lower_int_uint_mod(CodeGenT *cg, Expr a, Expr b);

class CodeGen_Vulkan_Dev : public CodeGen_GPU_Dev {
public:
    CodeGen_Vulkan_Dev(Target target);

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
        return "vulkan";
    }

protected:
    class SPIRV_Emitter : public IRVisitor {

    public:
        SPIRV_Emitter() = default;

        using IRVisitor::visit;

        void visit(const IntImm *) override;
        void visit(const UIntImm *) override;
        void visit(const FloatImm *) override;
        void visit(const StringImm *) override;
        void visit(const Cast *) override;
        void visit(const Reinterpret *) override;
        void visit(const Variable *) override;
        void visit(const Add *) override;
        void visit(const Sub *) override;
        void visit(const Mul *) override;
        void visit(const Div *) override;
        void visit(const Mod *) override;
        void visit(const Min *) override;
        void visit(const Max *) override;
        void visit(const EQ *) override;
        void visit(const NE *) override;
        void visit(const LT *) override;
        void visit(const LE *) override;
        void visit(const GT *) override;
        void visit(const GE *) override;
        void visit(const And *) override;
        void visit(const Or *) override;
        void visit(const Not *) override;
        void visit(const Select *) override;
        void visit(const Load *) override;
        void visit(const Ramp *) override;
        void visit(const Broadcast *) override;
        void visit(const Call *) override;
        void visit(const Let *) override;
        void visit(const LetStmt *) override;
        void visit(const AssertStmt *) override;
        void visit(const For *) override;
        void visit(const Store *) override;
        void visit(const Provide *) override;
        void visit(const Allocate *) override;
        void visit(const Free *) override;
        void visit(const Realize *) override;
        void visit(const ProducerConsumer *op) override;
        void visit(const IfThenElse *) override;
        void visit(const Evaluate *) override;
        void visit(const Shuffle *) override;
        void visit(const VectorReduce *) override;
        void visit(const Prefetch *) override;
        void visit(const Fork *) override;
        void visit(const Acquire *) override;
        void visit(const Atomic *) override;

        void visit_unary_op(SpvOp op_code, Type t, const Expr &a);
        void visit_binary_op(SpvOp op_code, Type t, const Expr &a, const Expr &b);
        void visit_glsl_op(SpvId glsl_op_code, Type t, const std::vector<Expr> &args);

        void load_from_scalar_index(const Load *op, SpvId index_id, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class);
        void load_from_vector_index(const Load *op, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class);

        void store_at_scalar_index(const Store *op, SpvId index_id, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class, SpvId value_id);
        void store_at_vector_index(const Store *op, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class, SpvId value_id);

        SpvFactory::Components split_vector(Type type, SpvId value_id);
        SpvId join_vector(Type type, const SpvFactory::Components &value_components);
        SpvId cast_type(Type target_type, Type value_type, SpvId value_id);

        using BuiltinMap = std::unordered_map<std::string, SpvId>;

        const BuiltinMap glsl_builtin = {
            {"pow_f16", GLSLstd450Pow},
            {"pow_f32", GLSLstd450Pow},
            {"acos_f16", GLSLstd450Acos},
            {"acos_f32", GLSLstd450Acos},
            {"acosh_f16", GLSLstd450Acosh},
            {"acosh_f32", GLSLstd450Acosh},
            {"asin_f16", GLSLstd450Asin},
            {"asin_f32", GLSLstd450Asin},
            {"asinh_f16", GLSLstd450Asinh},
            {"asinh_f32", GLSLstd450Asinh},
            {"atan2_f16", GLSLstd450Atan2},
            {"atan2_f32", GLSLstd450Atan2},
            {"atan_f16", GLSLstd450Atan},
            {"atan_f32", GLSLstd450Atan},
            {"atanh_f16", GLSLstd450Atanh},
            {"atanh_f32", GLSLstd450Atanh},
            {"ceil_f16", GLSLstd450Ceil},
            {"ceil_f32", GLSLstd450Ceil},
            {"cos_f16", GLSLstd450Cos},
            {"cos_f32", GLSLstd450Cos},
            {"cosh_f16", GLSLstd450Cosh},
            {"cosh_f32", GLSLstd450Cosh},
            {"exp_f16", GLSLstd450Exp},
            {"exp_f32", GLSLstd450Exp},
            {"fast_inverse_sqrt_f16", GLSLstd450InverseSqrt},
            {"fast_inverse_sqrt_f32", GLSLstd450InverseSqrt},
            {"floor_f16", GLSLstd450Floor},
            {"floor_f32", GLSLstd450Floor},
            {"log_f16", GLSLstd450Log},
            {"log_f32", GLSLstd450Log},
            {"sin_f16", GLSLstd450Sin},
            {"sin_f32", GLSLstd450Sin},
            {"sinh_f16", GLSLstd450Sinh},
            {"sinh_f32", GLSLstd450Sinh},
            {"sqrt_f16", GLSLstd450Sqrt},
            {"sqrt_f32", GLSLstd450Sqrt},
            {"tan_f16", GLSLstd450Tan},
            {"tan_f32", GLSLstd450Tan},
            {"tanh_f16", GLSLstd450Tanh},
            {"tanh_f32", GLSLstd450Tanh},
            {"trunc_f16", GLSLstd450Trunc},
            {"trunc_f32", GLSLstd450Trunc},
            {"mix", GLSLstd450FMix},
        };

        // The SPIRV-IR builder
        SpvBuilder builder;

        // Top-level function for adding kernels
        void add_kernel(const Stmt &s, const std::string &name, const std::vector<DeviceArgument> &args);
        void init_module();
        void compile(std::vector<char> &binary);
        void dump() const;

        // Scalarize expressions
        void scalarize(const Expr &e);
        SpvId map_type_to_pair(const Type &t);

        // Workgroup size
        void reset_workgroup_size();
        void declare_workgroup_size(SpvId kernel_func_id);
        void declare_entry_point(const Stmt &s, SpvId kernel_func_id);
        void declare_device_args(const Stmt &s, uint32_t entry_point_index, const std::string &kernel_name, const std::vector<DeviceArgument> &args);

        // The scope contains both the symbol id and its storage class
        using SymbolIdStorageClassPair = std::pair<SpvId, SpvStorageClass>;
        using SymbolScope = Scope<SymbolIdStorageClassPair>;
        using ScopedSymbolBinding = ScopedBinding<SymbolIdStorageClassPair>;
        SymbolScope symbol_table;

        // Map from a variable ID to its corresponding storage type definition
        struct StorageAccess {
            SpvStorageClass storage_class = SpvStorageClassMax;
            SpvId storage_type_id = SpvInvalidId;
            Type storage_type;
        };
        using StorageAccessMap = std::unordered_map<SpvId, StorageAccess>;
        StorageAccessMap storage_access_map;

        // Keep track of the descriptor sets so we can add a sidecar to the
        // module indicating which descriptor set to use for each entry point
        struct DescriptorSet {
            std::string entry_point_name;
            uint32_t uniform_buffer_count = 0;
            uint32_t storage_buffer_count = 0;
        };
        using DescriptorSetTable = std::vector<DescriptorSet>;
        DescriptorSetTable descriptor_set_table;

        // Encode the descriptor sets into a sidecar which will be added
        // as a header to the module prior to the actual SPIR-V binary
        void encode_header(SpvBinary &spirv_header);

        // The workgroup size.  May vary between kernels.
        uint32_t workgroup_size[3];

        // Returns Phi node inputs.
        template<typename StmtOrExpr>
        SpvFactory::BlockVariables emit_if_then_else(const Expr &condition, StmtOrExpr then_case, StmtOrExpr else_case);

    } emitter;

    std::string current_kernel_name;
};

// Check if all loads and stores to the member 'buffer' are dense, aligned, and
// have the same number of lanes. If this is indeed the case then the 'lanes'
// member stores the number of lanes in those loads and stores.
//
// FIXME: Refactor this and the version in CodeGen_OpenGLCompute_Dev to a common place!
//
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

    CheckAlignedDenseVectorLoadStore(std::string name)
        : buffer_name(std::move(name)) {
    }

private:
    // The name of the buffer to check.
    std::string buffer_name;

    using IRVisitor::visit;

    void visit(const Load *op) override {
        IRVisitor::visit(op);

        if (op->name != buffer_name) {
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

        if (op->name != buffer_name) {
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

void CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize(const Expr &e) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize(): " << (Expr)e << "\n";
    internal_assert(e.type().is_vector()) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize must be called with an expression of vector type.\n";

    SpvId type_id = builder.declare_type(e.type());
    SpvId value_id = builder.declare_null_constant(e.type());
    SpvId result_id = value_id;
    for (int i = 0; i < e.type().lanes(); i++) {
        extract_lane(e, i).accept(this);
        SpvId extracted_id = builder.current_id();
        SpvId composite_id = builder.reserve_id(SpvResultId);
        SpvFactory::Indices indices = {(uint32_t)i};
        builder.append(SpvFactory::composite_insert(type_id, composite_id, extracted_id, value_id, indices));
        result_id = composite_id;
    }
    builder.update_id(result_id);
}

SpvId CodeGen_Vulkan_Dev::SPIRV_Emitter::map_type_to_pair(const Type &t) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::map_type_to_pair(): " << t << "\n";
    SpvId base_type_id = builder.declare_type(t);
    SpvBuilder::StructMemberTypes member_type_ids = {base_type_id, base_type_id};
    const std::string struct_name = std::string("_struct_") + type_to_c_type(t, false, false) + std::string("_pair");
    SpvId struct_type_id = builder.declare_struct(struct_name, member_type_ids);
    return struct_type_id;
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Variable *var) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Variable): " << var->type << " " << var->name << "\n";
    SpvId variable_id = symbol_table.get(var->name).first;
    user_assert(variable_id != SpvInvalidId) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Variable): Invalid symbol name!\n";
    builder.update_id(variable_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const IntImm *imm) {
    if (imm->type.bits() == 8) {
        const int8_t value = (int8_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 16) {
        const int16_t value = (int16_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 32) {
        const int32_t value = (int32_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 64) {
        const int64_t value = (int64_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else {
        internal_error << "Vulkan backend currently only supports 8-bit, 16-bit, 32-bit or 64-bit signed integers!\n";
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const UIntImm *imm) {
    if (imm->type.bits() == 8) {
        const uint8_t value = (uint8_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 16) {
        const uint16_t value = (uint16_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 32) {
        const uint32_t value = (uint32_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 64) {
        const uint64_t value = (uint64_t)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else {
        internal_error << "Vulkan backend currently only supports 8-bit, 16-bit, 32-bit or 64-bit unsigned integers!\n";
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const StringImm *imm) {
    SpvId constant_id = builder.declare_string_constant(imm->value);
    builder.update_id(constant_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const FloatImm *imm) {
    if (imm->type.bits() == 32) {
        const float value = (float)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else if (imm->type.bits() == 64) {
        const double value = (double)(imm->value);
        SpvId constant_id = builder.declare_constant(imm->type, &value);
        builder.update_id(constant_id);
    } else {
        internal_error << "Vulkan backend currently only supports 32-bit or 64-bit floats\n";
    }
}

SpvId CodeGen_Vulkan_Dev::SPIRV_Emitter::cast_type(Type target_type, Type value_type, SpvId value_id) {

    if (value_type == target_type) {
        return value_id;
    }

    SpvOp op_code = SpvOpNop;
    if (value_type.is_float()) {
        if (target_type.is_float()) {
            op_code = SpvOpFConvert;
        } else if (target_type.is_uint()) {
            op_code = SpvOpConvertFToU;
        } else if (target_type.is_int()) {
            op_code = SpvOpConvertFToS;
        }
    } else if (value_type.is_uint()) {
        if (target_type.is_float()) {
            op_code = SpvOpConvertUToF;
        } else if (target_type.is_uint()) {
            if (target_type.bits() != value_type.bits()) {
                op_code = SpvOpUConvert;  // UConvert is only allowed on differing component widths
            }
        } else if (target_type.is_int()) {
            if (target_type.bits() != value_type.bits()) {
                op_code = SpvOpSConvert;  // SConvert is only allowed on differing component widths
            }
        }
    } else if (value_type.is_int()) {
        if (target_type.is_float()) {
            op_code = SpvOpConvertSToF;
        } else if (target_type.is_uint()) {
            if (target_type.bits() != value_type.bits()) {
                op_code = SpvOpUConvert;  // UConvert is only allowed on differing component widths
            }
        } else if (target_type.is_int() || target_type.is_uint()) {
            if (target_type.bits() != value_type.bits()) {
                op_code = SpvOpSConvert;  // SConvert is only allowed on differing component widths
            }
        }
    }

    // If none of the explicit conversions matched, do a direct bitcast if the total
    // size of both types is the same
    if (op_code == SpvOpNop) {
        if (target_type.bytes() == value_type.bytes()) {
            op_code = SpvOpBitcast;
        }
    }

    // Error If we still didn't find a suitable cast ...
    if (op_code == SpvOpNop) {
        user_error << "Unhandled type cast from value type '" << value_type << "' to target type '" << target_type << "'!";
        return SpvInvalidId;
    }

    SpvId target_type_id = builder.declare_type(target_type);
    SpvId result_id = builder.reserve_id(SpvResultId);
    if (op_code == SpvOpBitcast) {
        builder.append(SpvFactory::bitcast(target_type_id, result_id, value_id));
    } else {
        builder.append(SpvFactory::convert(op_code, target_type_id, result_id, value_id));
    }
    return result_id;
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Cast *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast): " << op->value.type() << " to " << op->type << "\n";

    Type value_type = op->value.type();
    Type target_type = op->type;

    op->value.accept(this);
    SpvId value_id = builder.current_id();

    if ((value_type.is_vector() && target_type.is_vector())) {
        if (value_type.lanes() == target_type.lanes()) {
            SpvId result_id = cast_type(target_type, value_type, value_id);
            builder.update_id(result_id);
        } else {
            user_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << " (incompatible lanes)\n";
        }
    } else if (value_type.is_scalar() && target_type.is_scalar()) {
        debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast): scalar type (cast)\n";
        SpvId result_id = cast_type(target_type, value_type, value_id);
        builder.update_id(result_id);
    } else if (value_type.bytes() == target_type.bytes()) {
        SpvId result_id = cast_type(target_type, value_type, value_id);
        builder.update_id(result_id);
    } else {
        user_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << "\n";
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Reinterpret *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Reinterpret): " << op->value.type() << " to " << op->type << "\n";
    SpvId type_id = builder.declare_type(op->type);
    op->value.accept(this);
    SpvId src_id = builder.current_id();
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::bitcast(type_id, result_id, src_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Add *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Add): " << op->type << " ((" << op->a << ") + (" << op->b << "))\n";
    visit_binary_op(op->type.is_float() ? SpvOpFAdd : SpvOpIAdd, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Sub *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Sub): " << op->type << " ((" << op->a << ") - (" << op->b << "))\n";
    visit_binary_op(op->type.is_float() ? SpvOpFSub : SpvOpISub, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Mul *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Mul): " << op->type << " ((" << op->a << ") * (" << op->b << "))\n";
    visit_binary_op(op->type.is_float() ? SpvOpFMul : SpvOpIMul, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Div *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Div): " << op->type << " ((" << op->a << ") / (" << op->b << "))\n";
    user_assert(!is_const_zero(op->b)) << "Division by constant zero in expression: " << Expr(op) << "\n";

    if (op->type.is_float()) {
        visit_binary_op(SpvOpFDiv, op->type, op->a, op->b);
    } else {
        Expr e = lower_int_uint_div(op->a, op->b);
        e.accept(this);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Mod *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Mod): " << op->type << " ((" << op->a << ") % (" << op->b << "))\n";
    if (op->type.is_float()) {
        // SPIR-V FMod is strangely not what we want .. FRem does what we need
        visit_binary_op(SpvOpFRem, op->type, op->a, op->b);
    } else {
        Expr e = lower_int_uint_mod(op->a, op->b);
        e.accept(this);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Max *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Max): " << op->type << " Max((" << op->a << "), (" << op->b << "))\n";
    SpvId op_code = SpvOpNop;
    if (op->type.is_float()) {
        op_code = GLSLstd450FMax;
    } else if (op->type.is_int()) {
        op_code = GLSLstd450SMax;
    } else if (op->type.is_uint()) {
        op_code = GLSLstd450UMax;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Max *op): unhandled type: " << op->type << "\n";
    }
    std::vector<Expr> args = {op->a, op->b};
    visit_glsl_op(op_code, op->type, args);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Min *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Min): " << op->type << " Min((" << op->a << "), (" << op->b << "))\n";
    SpvId op_code = SpvOpNop;
    if (op->type.is_float()) {
        op_code = GLSLstd450FMin;
    } else if (op->type.is_int()) {
        op_code = GLSLstd450SMin;
    } else if (op->type.is_uint()) {
        op_code = GLSLstd450UMin;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Min *op): unhandled type: " << op->type << "\n";
    }
    std::vector<Expr> args = {op->a, op->b};
    visit_glsl_op(op_code, op->type, args);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const EQ *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(EQ): " << op->type << " (" << op->a << ") == (" << op->b << ")\n";
    visit_binary_op(op->type.is_float() ? SpvOpFOrdEqual : SpvOpIEqual, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const NE *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(NE): " << op->type << " (" << op->a << ") != (" << op->b << ")\n";
    visit_binary_op(op->type.is_float() ? SpvOpFOrdNotEqual : SpvOpINotEqual, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const LT *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(LT): " << op->type << " (" << op->a << ") < (" << op->b << ")\n";
    SpvOp op_code = SpvOpNop;
    if (op->a.type().is_float()) {
        op_code = SpvOpFOrdLessThan;
    } else if (op->a.type().is_int()) {
        op_code = SpvOpSLessThan;
    } else if (op->a.type().is_uint()) {
        op_code = SpvOpULessThan;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const LT *op): unhandled type: " << op->a.type() << "\n";
    }
    visit_binary_op(op_code, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const LE *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(LE): " << op->type << " (" << op->a << ") <= (" << op->b << ")\n";
    SpvOp op_code = SpvOpNop;
    if (op->a.type().is_float()) {
        op_code = SpvOpFOrdLessThanEqual;
    } else if (op->a.type().is_int()) {
        op_code = SpvOpSLessThanEqual;
    } else if (op->a.type().is_uint()) {
        op_code = SpvOpULessThanEqual;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const LE *op): unhandled type: " << op->a.type() << "\n";
    }
    visit_binary_op(op_code, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const GT *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(GT): " << op->type << " (" << op->a << ") > (" << op->b << ")\n";
    SpvOp op_code = SpvOpNop;
    if (op->a.type().is_float()) {
        op_code = SpvOpFOrdGreaterThan;
    } else if (op->a.type().is_int()) {
        op_code = SpvOpSGreaterThan;
    } else if (op->a.type().is_uint()) {
        op_code = SpvOpUGreaterThan;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const GT *op): unhandled type: " << op->a.type() << "\n";
    }
    visit_binary_op(op_code, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const GE *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(GE): " << op->type << " (" << op->a << ") >= (" << op->b << ")\n";
    SpvOp op_code = SpvOpNop;
    if (op->a.type().is_float()) {
        op_code = SpvOpFOrdGreaterThanEqual;
    } else if (op->a.type().is_int()) {
        op_code = SpvOpSGreaterThanEqual;
    } else if (op->a.type().is_uint()) {
        op_code = SpvOpUGreaterThanEqual;
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const GE *op): unhandled type: " << op->a.type() << "\n";
    }
    visit_binary_op(op_code, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const And *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(And): " << op->type << " (" << op->a << ") && (" << op->b << ")\n";
    visit_binary_op(SpvOpLogicalAnd, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Or *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Or): " << op->type << " (" << op->a << ") || (" << op->b << ")\n";
    visit_binary_op(SpvOpLogicalOr, op->type, op->a, op->b);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Not *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Not): " << op->type << " !(" << op->a << ")\n";
    visit_unary_op(SpvOpLogicalNot, op->type, op->a);
}
void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const ProducerConsumer *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(ProducerConsumer): name=" << op->name << " is_producer=" << (op->is_producer ? "true" : "false") << "\n";
    op->body.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Call *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Call): " << op->type << " " << op->name << " args=" << (uint32_t)op->args.size() << "\n";

    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        internal_assert(op->args.size() == 1) << "gpu_thread_barrier() intrinsic must specify memory fence type.\n";

        const auto *fence_type_ptr = as_const_int(op->args[0]);
        internal_assert(fence_type_ptr) << "gpu_thread_barrier() parameter is not a constant integer.\n";
        auto fence_type = *fence_type_ptr;

        // Follow GLSL semantics for GLCompute ...
        //
        // barrier() -> control_barrier(Workgroup, Workgroup, AcquireRelease | WorkgroupMemory)
        //
        uint32_t execution_scope = SpvWorkgroupScope;
        uint32_t memory_scope = SpvWorkgroupScope;
        uint32_t control_mask = (SpvMemorySemanticsAcquireReleaseMask | SpvMemorySemanticsWorkgroupMemoryMask);
        SpvId exec_scope_id = builder.declare_constant(UInt(32), &execution_scope);
        SpvId memory_scope_id = builder.declare_constant(UInt(32), &memory_scope);
        SpvId control_mask_id = builder.declare_constant(UInt(32), &control_mask);
        builder.append(SpvFactory::control_barrier(exec_scope_id, memory_scope_id, control_mask_id));

        if ((fence_type & CodeGen_GPU_Dev::MemoryFenceType::Device) ||
            (fence_type & CodeGen_GPU_Dev::MemoryFenceType::Shared)) {

            // groupMemoryBarrier() -> memory_barrier(Workgroup, AcquireRelease | UniformMemory | WorkgroupMemory | ImageMemory)
            //
            uint32_t memory_mask = (SpvMemorySemanticsAcquireReleaseMask |
                                    SpvMemorySemanticsUniformMemoryMask |
                                    SpvMemorySemanticsWorkgroupMemoryMask |
                                    SpvMemorySemanticsImageMemoryMask);
            SpvId memory_mask_id = builder.declare_constant(UInt(32), &memory_mask);
            builder.append(SpvFactory::memory_barrier(memory_scope_id, memory_mask_id));
        }
        SpvId result_id = builder.declare_null_constant(op->type);
        builder.update_id(result_id);

    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);

        SpvId op_code = SpvInvalidId;
        if (op->type.is_float()) {
            op_code = GLSLstd450FAbs;
        } else {
            op_code = GLSLstd450SAbs;
        }
        visit_glsl_op(op_code, op->type, op->args);

    } else if (op->is_intrinsic(Call::IntrinsicOp::round)) {
        internal_assert(op->args.size() == 1);

        // GLSL RoundEven matches Halide's implementation
        visit_glsl_op(GLSLstd450RoundEven, op->type, op->args);

    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = cast(op->type, select(a < b, b - a, a - b));
        e->accept(this);
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        // Simply discard the first argument, which is generally a call to
        // 'halide_printf'.
        if (op->args[1].defined()) {
            op->args[1]->accept(this);
        }
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        visit_binary_op(SpvOpBitwiseAnd, op->type, op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        visit_binary_op(SpvOpBitwiseXor, op->type, op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        visit_binary_op(SpvOpBitwiseOr, op->type, op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        visit_unary_op(SpvOpNot, op->type, op->args[0]);
    } else if (op->is_intrinsic(Call::if_then_else)) {
        Expr cond = op->args[0];
        if (const Broadcast *b = cond.as<Broadcast>()) {
            cond = b->value;
        }
        if (cond.type().is_vector()) {
            scalarize(op);

        } else {
            // Generate Phi node if used as an expression.
            internal_assert(op->args.size() == 2 || op->args.size() == 3);
            Expr else_expr;
            if (op->args.size() == 3) {
                else_expr = op->args[2];
            }
            SpvFactory::BlockVariables block_vars = emit_if_then_else(op->args[0], op->args[1], else_expr);
            SpvId type_id = builder.declare_type(op->type);
            SpvId result_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::phi(type_id, result_id, block_vars));
            builder.update_id(result_id);
        }
    } else if (op->is_intrinsic(Call::IntrinsicOp::div_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        SpvOp op_code = SpvOpNop;
        if (op->type.is_float()) {
            op_code = SpvOpFDiv;
        } else if (op->type.is_int()) {
            op_code = SpvOpSDiv;
        } else if (op->type.is_uint()) {
            op_code = SpvOpUDiv;
        } else {
            internal_error << "div_round_to_zero of unhandled type.\n";
        }
        visit_binary_op(op_code, op->type, op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::IntrinsicOp::mod_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        SpvOp op_code = SpvOpNop;
        if (op->type.is_float()) {
            op_code = SpvOpFMod;
        } else if (op->type.is_int()) {
            op_code = SpvOpSMod;
        } else if (op->type.is_uint()) {
            op_code = SpvOpUMod;
        } else {
            internal_error << "mod_round_to_zero of unhandled type.\n";
        }
        visit_binary_op(op_code, op->type, op->args[0], op->args[1]);

    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        if (op->type.is_uint()) {
            visit_binary_op(SpvOpShiftRightLogical, op->type, op->args[0], op->args[1]);
        } else {
            visit_binary_op(SpvOpShiftRightArithmetic, op->type, op->args[0], op->args[1]);
        }
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        visit_binary_op(SpvOpShiftLeftLogical, op->type, op->args[0], op->args[1]);
    } else if (op->is_intrinsic(Call::strict_float)) {
        // TODO: Enable/Disable RelaxedPrecision flags?
        internal_assert(op->args.size() == 1);
        op->args[0].accept(this);
    } else if (op->is_intrinsic(Call::IntrinsicOp::sorted_avg)) {
        internal_assert(op->args.size() == 2);
        // b > a, so the following works without widening:
        // a + (b - a)/2
        Expr e = op->args[0] + (op->args[1] - op->args[0]) / 2;
        e.accept(this);
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
        e.accept(this);

    } else if (op->is_intrinsic(Call::mux)) {
        Expr e = lower_mux(op);
        e.accept(this);
    } else if (op->is_intrinsic(Call::widen_right_add) ||
               op->is_intrinsic(Call::widen_right_mul) ||
               op->is_intrinsic(Call::widen_right_sub) ||
               op->is_intrinsic(Call::widening_add) ||
               op->is_intrinsic(Call::widening_mul) ||
               op->is_intrinsic(Call::widening_sub) ||
               op->is_intrinsic(Call::widening_shift_left) ||
               op->is_intrinsic(Call::widening_shift_right) ||
               op->is_intrinsic(Call::rounding_shift_left) ||
               op->is_intrinsic(Call::rounding_shift_right) ||
               op->is_intrinsic(Call::saturating_cast) ||
               op->is_intrinsic(Call::saturating_add) ||
               op->is_intrinsic(Call::saturating_sub) ||
               op->is_intrinsic(Call::saturating_cast) ||
               op->is_intrinsic(Call::halving_add) ||
               op->is_intrinsic(Call::halving_sub) ||
               op->is_intrinsic(Call::rounding_halving_add) ||
               op->is_intrinsic(Call::mul_shift_right) ||
               op->is_intrinsic(Call::rounding_mul_shift_right)) {
        Expr e = lower_intrinsic(op);
        e.accept(this);
        return;

    } else if (op->name == "nan_f32") {
        float value = NAN;
        SpvId result_id = builder.declare_constant(Float(32), &value);
        builder.update_id(result_id);
    } else if (op->name == "inf_f32") {
        float value = INFINITY;
        SpvId result_id = builder.declare_constant(Float(32), &value);
        builder.update_id(result_id);
    } else if (op->name == "neg_inf_f32") {
        float value = -INFINITY;
        SpvId result_id = builder.declare_constant(Float(32), &value);
        builder.update_id(result_id);
    } else if (starts_with(op->name, "is_nan_f")) {
        internal_assert(op->args.size() == 1);
        visit_unary_op((SpvOp)SpvOpIsNan, op->type, op->args[0]);
    } else if (starts_with(op->name, "is_inf_f")) {
        internal_assert(op->args.size() == 1);
        visit_unary_op((SpvOp)SpvOpIsInf, op->type, op->args[0]);
    } else if (starts_with(op->name, "is_finite_f")) {

        internal_assert(op->args.size() == 1);
        visit_unary_op((SpvOp)SpvOpIsInf, op->type, op->args[0]);
        SpvId is_inf_id = builder.current_id();
        visit_unary_op((SpvOp)SpvOpIsNan, op->type, op->args[0]);
        SpvId is_nan_id = builder.current_id();

        SpvId type_id = builder.declare_type(op->type);
        SpvId not_is_nan_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::logical_not(type_id, not_is_nan_id, is_nan_id));
        SpvId not_is_inf_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::logical_not(type_id, not_is_inf_id, is_inf_id));
        SpvId result_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::logical_and(type_id, result_id, not_is_inf_id, not_is_nan_id));
        builder.update_id(result_id);

    } else {

        // If its not a standard SPIR-V built-in, see if there's a GLSL extended builtin
        BuiltinMap::const_iterator glsl_it = glsl_builtin.find(op->name);
        if (glsl_it == glsl_builtin.end()) {
            user_error << "Vulkan: unhandled SPIR-V GLSL builtin function '" << op->name << "' encountered.\n";
        }

        // Call the GLSL extended built-in
        SpvId glsl_op_code = glsl_it->second;
        visit_glsl_op(glsl_op_code, op->type, op->args);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Select *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Select): " << op->type << " (" << op->condition << ") ? (" << op->true_value << ") : (" << op->false_value << ")\n";
    SpvId type_id = builder.declare_type(op->type);
    op->condition.accept(this);
    SpvId cond_id = builder.current_id();
    op->true_value.accept(this);
    SpvId true_id = builder.current_id();
    op->false_value.accept(this);
    SpvId false_id = builder.current_id();
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::select(type_id, result_id, cond_id, true_id, false_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::load_from_scalar_index(const Load *op, SpvId index_id, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::load_from_scalar_index(): "
             << "index_id=" << index_id << " "
             << "variable_id=" << variable_id << " "
             << "value_type=" << value_type << " "
             << "storage_type=" << storage_type << " "
             << "storage_class=" << storage_class << "\n";

    // determine the base type id for the source value
    SpvId base_type_id = builder.type_of(variable_id);
    if (builder.is_pointer_type(base_type_id)) {
        base_type_id = builder.lookup_base_type(base_type_id);
    }

    SpvId storage_type_id = builder.declare_type(storage_type);
    SpvId ptr_type_id = builder.declare_pointer_type(storage_type, storage_class);

    uint32_t zero = 0;
    SpvId src_id = SpvInvalidId;
    SpvId src_index_id = index_id;
    if (storage_class == SpvStorageClassUniform) {
        if (builder.is_struct_type(base_type_id)) {
            SpvId zero_id = builder.declare_constant(UInt(32), &zero);
            SpvFactory::Indices access_indices = {zero_id, src_index_id};
            src_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        } else {
            SpvFactory::Indices access_indices = {src_index_id};
            src_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        }
    } else if ((storage_class == SpvStorageClassWorkgroup) || (storage_class == SpvStorageClassFunction)) {
        if (builder.is_array_type(base_type_id)) {
            SpvFactory::Indices access_indices = {src_index_id};
            src_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        } else {
            src_id = variable_id;
        }
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Load): unhandled storage class encountered on op: " << storage_class << "\n";
    }
    internal_assert(src_id != SpvInvalidId);

    SpvId value_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::load(storage_type_id, value_id, src_id));

    // if the value type doesn't match the base for the pointer type, cast it accordingly
    SpvId result_id = value_id;
    if (storage_type != value_type) {
        result_id = cast_type(value_type, storage_type, result_id);
    }
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::load_from_vector_index(const Load *op, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::load_from_vector_index(): "
             << "variable_id=" << variable_id << " "
             << "value_type=" << value_type << " "
             << "storage_type=" << storage_type << " "
             << "storage_class=" << storage_class << "\n";

    internal_assert(op->index.type().is_vector());

    // If the runtime array is a vector type, then attempt to do a
    // dense vector load by using the base of the ramp divided by
    // the number of lanes.
    StorageAccessMap::const_iterator it = storage_access_map.find(variable_id);
    if (it != storage_access_map.end()) {
        storage_type = it->second.storage_type;  // use the storage type for the runtime array
        SpvId storage_type_id = it->second.storage_type_id;
        if (builder.is_vector_type(storage_type_id)) {
            Expr ramp_base = strided_ramp_base(op->index);
            if (ramp_base.defined()) {
                Expr ramp_index = (ramp_base / op->type.lanes());
                ramp_index.accept(this);
                SpvId index_id = builder.current_id();
                load_from_scalar_index(op, index_id, variable_id, value_type, storage_type, storage_class);
                return;
            }
        }
    }

    op->index.accept(this);
    SpvId index_id = builder.current_id();

    // Gather vector elements.
    SpvFactory::Components loaded_values;
    Type scalar_value_type = value_type.with_lanes(1);
    SpvFactory::Components index_components = split_vector(op->index.type(), index_id);
    for (SpvId scalar_index : index_components) {
        load_from_scalar_index(op, scalar_index, variable_id, scalar_value_type, storage_type, storage_class);
        SpvId value_component_id = builder.current_id();
        loaded_values.push_back(value_component_id);
    }

    // Create a composite vector from the individual loads
    if (loaded_values.size() > 1) {
        SpvId result_id = join_vector(value_type, loaded_values);
        builder.update_id(result_id);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::store_at_scalar_index(const Store *op, SpvId index_id, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class, SpvId value_id) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::store_at_scalar_index(): "
             << "index_id=" << index_id << " "
             << "variable_id=" << variable_id << " "
             << "value_type=" << value_type << " "
             << "storage_type=" << storage_type << " "
             << "storage_class=" << storage_class << " "
             << "value_id=" << value_id << "\n";

    // determine the base type id for the source value
    SpvId base_type_id = builder.type_of(variable_id);
    if (builder.is_pointer_type(base_type_id)) {
        base_type_id = builder.lookup_base_type(base_type_id);
    }

    uint32_t zero = 0;
    SpvId dst_id = SpvInvalidId;
    SpvId dst_index_id = index_id;

    SpvId ptr_type_id = builder.declare_pointer_type(storage_type, storage_class);
    if (storage_class == SpvStorageClassUniform) {
        if (builder.is_struct_type(base_type_id)) {
            SpvId zero_id = builder.declare_constant(UInt(32), &zero);
            SpvFactory::Indices access_indices = {zero_id, dst_index_id};
            dst_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        } else {
            SpvFactory::Indices access_indices = {dst_index_id};
            dst_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        }
    } else if ((storage_class == SpvStorageClassWorkgroup) || (storage_class == SpvStorageClassFunction)) {
        if (builder.is_array_type(base_type_id)) {
            SpvFactory::Indices access_indices = {dst_index_id};
            dst_id = builder.declare_access_chain(ptr_type_id, variable_id, access_indices);
        } else {
            dst_id = variable_id;
        }
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): unhandled storage class encountered on op: " << storage_class << "\n";
    }
    internal_assert(dst_id != SpvInvalidId);

    // if the value type doesn't match the base for the pointer type, cast it accordingly
    if (storage_type != value_type) {
        value_id = cast_type(storage_type, value_type, value_id);
    }

    builder.append(SpvFactory::store(dst_id, value_id));
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::store_at_vector_index(const Store *op, SpvId variable_id, Type value_type, Type storage_type, SpvStorageClass storage_class, SpvId value_id) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::store_at_vector_index(): "
             << "variable_id=" << variable_id << " "
             << "value_type=" << value_type << " "
             << "storage_type=" << storage_type << " "
             << "storage_class=" << storage_class << "\n";

    internal_assert(op->index.type().is_vector());

    // If the runtime array is a vector type, then attempt to do a
    // dense vector store by using the base of the ramp divided by
    // the number of lanes.
    StorageAccessMap::const_iterator it = storage_access_map.find(variable_id);
    if (it != storage_access_map.end()) {
        storage_type = it->second.storage_type;
        SpvId storage_type_id = it->second.storage_type_id;
        if (builder.is_vector_type(storage_type_id)) {
            Expr ramp_base = strided_ramp_base(op->index);
            if (ramp_base.defined()) {
                Expr ramp_index = (ramp_base / op->value.type().lanes());
                ramp_index.accept(this);
                SpvId index_id = builder.current_id();
                store_at_scalar_index(op, index_id, variable_id, value_type, storage_type, storage_class, value_id);
                return;
            }
        }
    }

    op->index.accept(this);
    SpvId index_id = builder.current_id();

    // Split vector value into components
    internal_assert(op->index.type().lanes() <= op->value.type().lanes());
    SpvFactory::Components value_components = split_vector(op->value.type(), value_id);
    SpvFactory::Components index_components = split_vector(op->index.type(), index_id);

    // Scatter vector elements.
    Type scalar_value_type = op->value.type().with_lanes(1);
    for (uint32_t i = 0; i < index_components.size(); i++) {
        SpvId index_component_id = index_components[i];
        SpvId value_component_id = value_components[i];
        store_at_scalar_index(op, index_component_id, variable_id, scalar_value_type, storage_type, storage_class, value_component_id);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Load *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Load): " << op->type << " " << op->name << "[" << op->index << "]\n";
    user_assert(is_const_one(op->predicate)) << "Predicated loads not supported by SPIR-V codegen\n";

    // Construct the pointer to read from
    internal_assert(symbol_table.contains(op->name));
    SymbolIdStorageClassPair id_and_storage_class = symbol_table.get(op->name);
    SpvId variable_id = id_and_storage_class.first;
    SpvStorageClass storage_class = id_and_storage_class.second;
    internal_assert(variable_id != SpvInvalidId);
    internal_assert(((uint32_t)storage_class) < ((uint32_t)SpvStorageClassMax));

    // If this is a load from a buffer block (mapped to a halide buffer) or
    // GPU shared memory, the pointer type must match the declared storage
    // type for the runtime array.
    Type value_type = op->type;
    Type storage_type = value_type;
    StorageAccessMap::const_iterator it = storage_access_map.find(variable_id);
    if (it != storage_access_map.end()) {
        storage_type = it->second.storage_type;
    }

    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Load): value_type=" << op->type << " storage_type=" << storage_type << "\n";
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Load): index_type=" << op->index.type() << " index=" << op->index << "\n";

    if (op->index.type().is_scalar()) {
        op->index.accept(this);
        SpvId index_id = builder.current_id();
        load_from_scalar_index(op, index_id, variable_id, value_type, storage_type, storage_class);
    } else {
        load_from_vector_index(op, variable_id, value_type, storage_type, storage_class);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Store *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): " << op->name << "[" << op->index << "] = (" << op->value << ")\n";
    user_assert(is_const_one(op->predicate)) << "Predicated stores not supported by SPIR-V codegen!\n";

    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): value_type=" << op->value.type() << " value=" << op->value << "\n";
    op->value.accept(this);
    SpvId value_id = builder.current_id();

    internal_assert(symbol_table.contains(op->name));
    SymbolIdStorageClassPair id_and_storage_class = symbol_table.get(op->name);
    SpvId variable_id = id_and_storage_class.first;
    SpvStorageClass storage_class = id_and_storage_class.second;
    internal_assert(variable_id != SpvInvalidId);
    internal_assert(((uint32_t)storage_class) < ((uint32_t)SpvStorageClassMax));

    Type value_type = op->value.type();
    Type storage_type = value_type;

    // If this is a store to a buffer block (mapped to a halide buffer) or
    // GPU shared memory, the pointer type must match the declared storage
    // type for the runtime array
    StorageAccessMap::const_iterator it = storage_access_map.find(variable_id);
    if (it != storage_access_map.end()) {
        storage_type = it->second.storage_type;
    }

    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): value_type=" << value_type << " storage_type=" << storage_type << "\n";
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): index_type=" << op->index.type() << " index=" << op->index << "\n";
    if (op->index.type().is_scalar()) {
        op->index.accept(this);
        SpvId index_id = builder.current_id();
        store_at_scalar_index(op, index_id, variable_id, value_type, storage_type, storage_class, value_id);
    } else {
        store_at_vector_index(op, variable_id, value_type, storage_type, storage_class, value_id);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Let *let) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Let): " << (Expr)let << "\n";
    let->value.accept(this);
    SpvId current_id = builder.current_id();
    ScopedSymbolBinding binding(symbol_table, let->name, {current_id, SpvStorageClassFunction});
    let->body.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const LetStmt *let) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(LetStmt): " << let->name << "\n";
    let->value.accept(this);
    SpvId current_id = builder.current_id();
    ScopedSymbolBinding binding(symbol_table, let->name, {current_id, SpvStorageClassFunction});
    let->body.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const AssertStmt *stmt) {
    // TODO: Fill this in.
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(AssertStmt): "
             << "condition=" << stmt->condition << " "
             << "message=" << stmt->message << "\n";
}

namespace {
std::pair<std::string, uint32_t> simt_intrinsic(const std::string &name) {
    if (ends_with(name, ".__thread_id_x")) {
        return {"LocalInvocationId", 0};
    } else if (ends_with(name, ".__thread_id_y")) {
        return {"LocalInvocationId", 1};
    } else if (ends_with(name, ".__thread_id_z")) {
        return {"LocalInvocationId", 2};
    } else if (ends_with(name, ".__block_id_x")) {
        return {"WorkgroupId", 0};
    } else if (ends_with(name, ".__block_id_y")) {
        return {"WorkgroupId", 1};
    } else if (ends_with(name, ".__block_id_z")) {
        return {"WorkgroupId", 2};
    } else if (ends_with(name, "id_w")) {
        user_error << "Vulkan only supports <=3 dimensions for gpu blocks";
    }
    internal_error << "simt_intrinsic called on bad variable name: " << name << "\n";
    return {"", -1};
}

int thread_loop_workgroup_index(const std::string &name) {
    std::string ids[] = {".__thread_id_x",
                         ".__thread_id_y",
                         ".__thread_id_z"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(std::string); i++) {
        if (ends_with(name, ids[i])) {
            return i;
        }
    }
    return -1;
}

}  // anonymous namespace

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const For *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(For): " << op->name << "\n";

    if (is_gpu_var(op->name)) {
        internal_assert((op->for_type == ForType::GPUBlock) ||
                        (op->for_type == ForType::GPUThread))
            << "kernel loops must be either gpu block or gpu thread\n";
        // This should always be true at this point in codegen
        internal_assert(is_const_zero(op->min));

        // Save & validate the workgroup size
        int idx = thread_loop_workgroup_index(op->name);
        if (idx >= 0) {
            const IntImm *wsize = op->extent.as<IntImm>();
            user_assert(wsize != nullptr) << "Vulkan requires statically-known workgroup size.\n";
            uint32_t new_wsize = wsize->value;
            user_assert(workgroup_size[idx] == 0 || workgroup_size[idx] == new_wsize) << "Vulkan requires all kernels have the same workgroup size, but two different ones "
                                                                                         "were encountered "
                                                                                      << workgroup_size[idx] << " and " << new_wsize << " in dimension " << idx << "\n";
            workgroup_size[idx] = new_wsize;
        }

        auto intrinsic = simt_intrinsic(op->name);

        // Intrinsics are inserted when adding the kernel
        internal_assert(symbol_table.contains(intrinsic.first));
        SpvId intrinsic_id = symbol_table.get(intrinsic.first).first;

        // extract and cast to int (which is what's expected by Halide's for loops)
        SpvId unsigned_type_id = builder.declare_type(UInt(32));
        SpvId unsigned_gpu_var_id = builder.reserve_id(SpvResultId);
        SpvId signed_type_id = builder.declare_type(Int(32));
        SpvId signed_gpu_var_id = builder.reserve_id(SpvResultId);
        SpvFactory::Indices indices = {intrinsic.second};
        builder.append(SpvFactory::composite_extract(unsigned_type_id, unsigned_gpu_var_id, intrinsic_id, indices));
        builder.append(SpvFactory::bitcast(signed_type_id, signed_gpu_var_id, unsigned_gpu_var_id));
        {
            ScopedSymbolBinding binding(symbol_table, op->name, {signed_gpu_var_id, SpvStorageClassUniform});
            op->body.accept(this);
        }

    } else {

        debug(2) << "  (serial for loop): min=" << op->min << " extent=" << op->extent << "\n";

        internal_assert(op->for_type == ForType::Serial) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit unhandled For type: " << op->for_type << "\n";
        user_assert(op->min.type() == op->extent.type());
        user_assert(op->min.type().is_int() || op->min.type().is_uint());

        op->min.accept(this);
        SpvId min_id = builder.current_id();
        op->extent.accept(this);
        SpvId extent_id = builder.current_id();

        // Compute max.
        Type index_type = Int(32);
        SpvId index_type_id = builder.declare_type(index_type);
        SpvStorageClass storage_class = SpvStorageClassFunction;
        SpvId index_var_type_id = builder.declare_pointer_type(index_type_id, storage_class);
        SpvId max_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::integer_add(index_type_id, max_id, min_id, extent_id));

        // Declare loop var
        const std::string loop_var_name = unique_name("_loop_idx");
        SpvId loop_var_id = builder.declare_variable(loop_var_name, index_var_type_id, storage_class, min_id);
        symbol_table.push(loop_var_name, {loop_var_id, storage_class});

        SpvId header_block_id = builder.reserve_id(SpvBlockId);
        SpvId top_block_id = builder.reserve_id(SpvBlockId);
        SpvId body_block_id = builder.reserve_id(SpvBlockId);
        SpvId continue_block_id = builder.reserve_id(SpvBlockId);
        SpvId merge_block_id = builder.reserve_id(SpvBlockId);

        SpvBlock header_block = builder.create_block(header_block_id);
        builder.enter_block(header_block);
        {
            builder.append(SpvFactory::loop_merge(merge_block_id, continue_block_id, SpvLoopControlDontUnrollMask));
            builder.append(SpvFactory::branch(top_block_id));
        }
        builder.leave_block();

        SpvId current_index_id = builder.reserve_id(SpvResultId);
        SpvBlock top_block = builder.create_block(top_block_id);
        builder.enter_block(top_block);
        {
            SpvId loop_test_type_id = builder.declare_type(Bool());
            SpvId loop_test_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::load(index_type_id, current_index_id, loop_var_id));
            builder.append(SpvFactory::less_than(loop_test_type_id, loop_test_id, current_index_id, max_id, true));
            builder.append(SpvFactory::conditional_branch(loop_test_id, body_block_id, merge_block_id));
        }
        builder.leave_block();

        SpvBlock body_block = builder.create_block(body_block_id);
        builder.enter_block(body_block);
        {
            ScopedSymbolBinding binding(symbol_table, op->name, {current_index_id, storage_class});
            op->body.accept(this);
            builder.append(SpvFactory::branch(continue_block_id));
        }
        builder.leave_block();

        SpvBlock continue_block = builder.create_block(continue_block_id);
        builder.enter_block(continue_block);
        {
            // Update loop variable
            int32_t one = 1;
            SpvId next_index_id = builder.reserve_id(SpvResultId);
            SpvId constant_one_id = builder.declare_constant(Int(32), &one);
            builder.append(SpvFactory::integer_add(index_type_id, next_index_id, current_index_id, constant_one_id));
            builder.append(SpvFactory::store(loop_var_id, next_index_id));
            builder.append(SpvFactory::branch(header_block_id));
        }
        builder.leave_block();
        symbol_table.pop(loop_var_name);

        SpvBlock merge_block = builder.create_block(merge_block_id);
        builder.enter_block(merge_block);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Ramp *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Ramp): "
             << "base=" << op->base << " "
             << "stride=" << op->stride << " "
             << "lanes=" << (uint32_t)op->lanes << "\n";

    // TODO: Is there a way to do this that doesn't require duplicating lane values?
    SpvId base_type_id = builder.declare_type(op->base.type());
    SpvId type_id = builder.declare_type(op->type);
    op->base.accept(this);
    SpvId base_id = builder.current_id();
    op->stride.accept(this);
    SpvId stride_id = builder.current_id();

    // Generate adds to make the elements of the ramp.
    SpvId prev_id = base_id;
    SpvFactory::Components constituents = {base_id};
    for (int i = 1; i < op->lanes; i++) {
        SpvId this_id = builder.reserve_id(SpvResultId);
        if (op->base.type().is_float()) {
            builder.append(SpvFactory::float_add(base_type_id, this_id, prev_id, stride_id));
        } else {
            builder.append(SpvFactory::integer_add(base_type_id, this_id, prev_id, stride_id));
        }
        constituents.push_back(this_id);
        prev_id = this_id;
    }

    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::composite_construct(type_id, result_id, constituents));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Broadcast *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Broadcast): "
             << "type=" << op->type << " "
             << "value=" << op->value << "\n";

    // TODO: Is there a way to do this that doesn't require duplicating lane values?
    SpvId type_id = builder.declare_type(op->type);
    op->value.accept(this);
    SpvId value_id = builder.current_id();
    SpvId result_id = builder.reserve_id(SpvResultId);

    SpvFactory::Components constituents;
    constituents.insert(constituents.end(), op->lanes, value_id);
    builder.append(SpvFactory::composite_construct(type_id, result_id, constituents));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Provide *) {
    internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Provide *): Provide encountered during codegen\n";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Allocate *op) {

    SpvId storage_type_id = builder.declare_type(op->type);
    SpvId array_type_id = SpvInvalidId;
    SpvId variable_id = SpvInvalidId;
    SpvStorageClass storage_class = SpvStorageClassGeneric;
    if (op->memory_type == MemoryType::GPUShared) {
        // Allocation of shared memory must be declared at global scope
        user_assert(op->extents.size() == 1 && is_const(op->extents[0]))
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported with Vulkan.";

        int32_t size = op->constant_allocation_size();
        array_type_id = builder.declare_type(op->type, size);
        storage_class = SpvStorageClassWorkgroup;  // shared across workgroup
        debug(2) << "Vulkan: Allocate " << op->name << "[" << (uint32_t)size << "] in shared memory on device in global scope\n";
        SpvId ptr_type_id = builder.declare_pointer_type(array_type_id, storage_class);
        variable_id = builder.declare_global_variable(op->name, ptr_type_id, storage_class);

    } else {

        // Allocation is not a shared memory allocation, just make a local declaration.
        debug(2) << "Vulkan: Allocate " << op->name << " on device in function scope\n";
        int32_t size = op->constant_allocation_size();

        // It must have a constant size.
        user_assert(size > 0)
            << "Allocation " << op->name << " has a dynamic size. "
            << "Only fixed-size allocations are supported with Vulkan.";

        array_type_id = builder.declare_type(op->type, size);
        storage_class = SpvStorageClassFunction;  // function scope
        SpvId ptr_type_id = builder.declare_pointer_type(array_type_id, storage_class);
        variable_id = builder.declare_variable(op->name, ptr_type_id, storage_class);
    }

    StorageAccess access;
    access.storage_class = storage_class;
    access.storage_type_id = storage_type_id;
    access.storage_type = op->type;
    storage_access_map[variable_id] = access;

    debug(3) << "Vulkan: Pushing allocation called " << op->name << " onto the symbol table\n";
    symbol_table.push(op->name, {variable_id, storage_class});
    op->body.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Free *op) {
    debug(3) << "Vulkan: Popping allocation called " << op->name << " off the symbol table\n";
    internal_assert(symbol_table.contains(op->name));
    SpvId variable_id = symbol_table.get(op->name).first;
    storage_access_map.erase(variable_id);
    symbol_table.pop(op->name);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Realize *) {
    internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Realize *): Realize encountered during codegen\n";
}

template<typename StmtOrExpr>
SpvFactory::BlockVariables
CodeGen_Vulkan_Dev::SPIRV_Emitter::emit_if_then_else(const Expr &condition,
                                                     StmtOrExpr then_case, StmtOrExpr else_case) {
    condition.accept(this);
    SpvId cond_id = builder.current_id();
    SpvId merge_block_id = builder.reserve_id(SpvBlockId);
    SpvId if_block_id = builder.reserve_id(SpvBlockId);
    SpvId then_block_id = builder.reserve_id(SpvBlockId);
    SpvId else_block_id = else_case.defined() ? builder.reserve_id(SpvBlockId) : merge_block_id;

    SpvFactory::BlockVariables block_vars;

    // If Conditional
    SpvBlock if_block = builder.create_block(if_block_id);
    builder.enter_block(if_block);
    {
        debug(2) << "Vulkan: If (" << condition << " )\n";

        builder.append(SpvFactory::selection_merge(merge_block_id, SpvSelectionControlMaskNone));
        builder.append(SpvFactory::conditional_branch(cond_id, then_block_id, else_block_id));
    }
    builder.leave_block();

    // Then block
    SpvBlock then_block = builder.create_block(then_block_id);
    builder.enter_block(then_block);
    {
        then_case.accept(this);
        debug(2) << "Vulkan: Then {" << then_case << " }\n";

        SpvId then_id = builder.current_id();
        builder.append(SpvFactory::branch(merge_block_id));
        block_vars.push_back({then_id, then_block_id});
    }
    builder.leave_block();

    // Else block (optional)
    if (else_case.defined()) {
        SpvBlock else_block = builder.create_block(else_block_id);
        builder.enter_block(else_block);
        {
            else_case.accept(this);
            debug(2) << "Vulkan: Else { " << else_case << " }\n";
            SpvId else_id = builder.current_id();
            builder.append(SpvFactory::branch(merge_block_id));
            block_vars.push_back({else_id, else_block_id});
        }
        builder.leave_block();
    }

    // Merge block
    SpvBlock merge_block = builder.create_block(merge_block_id);
    builder.enter_block(merge_block);
    return block_vars;
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const IfThenElse *op) {
    if (!builder.current_function().is_defined()) {
        user_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const IfThenElse *op): No active function for building!!\n";
    }
    emit_if_then_else(op->condition, op->then_case, op->else_case);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Evaluate *op) {
    op->value.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Shuffle *op) {
    std::cout << " CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Shuffle): "
              << "type=" << op->type << " "
              << "vectors=" << (uint32_t)op->vectors.size() << " "
              << "is_interleave=" << (op->is_interleave() ? "true" : "false") << " "
              << "is_extract_element=" << (op->is_extract_element() ? "true" : "false") << "\n";

    // Traverse all the arg vectors
    uint32_t arg_idx = 0;
    SpvFactory::Operands arg_ids;
    arg_ids.reserve(op->vectors.size());
    for (const Expr &e : op->vectors) {
        debug(2) << " CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Shuffle): Arg[" << arg_idx++ << "] => " << e << "\n";
        e.accept(this);
        arg_ids.push_back(builder.current_id());
    }

    if (op->is_interleave()) {
        int op_lanes = op->type.lanes();
        internal_assert(!arg_ids.empty());
        int arg_lanes = op->vectors[0].type().lanes();

        std::cout << "    vector interleave x" << (uint32_t)op->vectors.size() << " : ";
        for (int idx : op->indices) {
            std::cout << idx << " ";
        }
        std::cout << "\n";

        if (arg_ids.size() == 1) {

            // 1 argument, just do a simple assignment via a cast
            SpvId result_id = cast_type(op->type, op->vectors[0].type(), arg_ids[0]);
            builder.update_id(result_id);

        } else if (arg_ids.size() == 2) {

            // 2 arguments, use a composite insert to update even and odd indices
            uint32_t even_idx = 0;
            uint32_t odd_idx = 1;
            SpvFactory::Indices even_indices;
            SpvFactory::Indices odd_indices;
            for (int i = 0; i < op_lanes; ++i) {
                even_indices.push_back(even_idx);
                odd_indices.push_back(odd_idx);
                even_idx += 2;
                odd_idx += 2;
            }

            SpvId type_id = builder.declare_type(op->type);
            SpvId value_id = builder.declare_null_constant(op->type);
            SpvId partial_id = builder.reserve_id(SpvResultId);
            SpvId result_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::composite_insert(type_id, partial_id, arg_ids[0], value_id, even_indices));
            builder.append(SpvFactory::composite_insert(type_id, result_id, arg_ids[1], partial_id, odd_indices));
            builder.update_id(result_id);

        } else {
            // 3+ arguments, shuffle via a vector literal
            // selecting the appropriate elements of the vectors
            int num_vectors = (int)op->vectors.size();
            std::vector<SpvFactory::Components> vector_component_ids(num_vectors);
            for (uint32_t i = 0; i < (uint32_t)arg_ids.size(); ++i) {
                if (op->vectors[i].type().is_vector()) {
                    vector_component_ids[i] = split_vector(op->vectors[i].type(), arg_ids[i]);
                } else {
                    vector_component_ids[i] = {arg_ids[i]};
                }
            }

            SpvFactory::Components result_component_ids(op_lanes);
            for (int i = 0; i < op_lanes; i++) {
                int arg = i % num_vectors;
                int arg_idx = i / num_vectors;
                internal_assert(arg_idx <= arg_lanes);
                result_component_ids[i] = vector_component_ids[arg][arg_idx];
            }

            SpvId result_id = join_vector(op->type, result_component_ids);
            builder.update_id(result_id);
        }
    } else if (op->is_extract_element()) {
        int idx = op->indices[0];
        internal_assert(idx >= 0);
        internal_assert(idx <= op->vectors[0].type().lanes());
        if (op->vectors[0].type().is_vector()) {
            SpvFactory::Indices indices = {(uint32_t)idx};
            SpvId type_id = builder.declare_type(op->type);
            SpvId result_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::composite_extract(type_id, result_id, arg_ids[0], indices));
            builder.update_id(result_id);
        } else {
            SpvId result_id = cast_type(op->type, op->vectors[0].type(), arg_ids[0]);
            builder.update_id(result_id);
        }
    } else if (op->type.is_scalar()) {
        // Deduce which vector we need. Apparently it's not required
        // that all vectors have identical lanes, so a loop is required.
        // Since idx of -1 means "don't care", we'll treat it as 0 to simplify.
        SpvId result_id = SpvInvalidId;
        int idx = std::max(0, op->indices[0]);
        for (size_t vec_idx = 0; vec_idx < op->vectors.size(); vec_idx++) {
            const int vec_lanes = op->vectors[vec_idx].type().lanes();
            if (idx < vec_lanes) {
                if (op->vectors[vec_idx].type().is_vector()) {
                    SpvFactory::Indices indices = {(uint32_t)idx};
                    SpvId type_id = builder.declare_type(op->type);
                    result_id = builder.reserve_id(SpvResultId);
                    builder.append(SpvFactory::composite_extract(type_id, result_id, arg_ids[vec_idx], indices));
                } else {
                    result_id = arg_ids[vec_idx];
                }
                break;
            }
            idx -= vec_lanes;
        }

    } else {

        // vector shuffle ... not interleaving
        int op_lanes = op->type.lanes();
        int num_vectors = (int)op->vectors.size();

        std::cout << "    vector shuffle x" << num_vectors << " : ";
        for (int idx : op->indices) {
            std::cout << idx << " ";
        }
        std::cout << "\n";

        if (num_vectors == 1) {
            // 1 argument, just do a simple assignment via a cast
            SpvId result_id = cast_type(op->type, op->vectors[0].type(), arg_ids[0]);
            builder.update_id(result_id);

        } else if (num_vectors == 2) {

            // 2 arguments, use the builtin vector shuffle that takes a pair of vectors
            SpvFactory::Indices indices;
            indices.reserve(op->indices.size());
            indices.insert(indices.end(), op->indices.begin(), op->indices.end());
            SpvId type_id = builder.declare_type(op->type);
            SpvId result_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::vector_shuffle(type_id, result_id, arg_ids[0], arg_ids[1], indices));
            builder.update_id(result_id);
        } else {
            std::vector<SpvFactory::Components> vector_component_ids(num_vectors);
            for (uint32_t i = 0; i < (uint32_t)arg_ids.size(); ++i) {
                if (op->vectors[i].type().is_vector()) {
                    vector_component_ids[i] = split_vector(op->vectors[i].type(), arg_ids[i]);
                } else {
                    vector_component_ids[i] = {arg_ids[i]};
                }
            }

            SpvFactory::Components result_component_ids(op_lanes);
            for (int i = 0; i < op_lanes && i < (int)op->indices.size(); i++) {
                int idx = op->indices[i];
                int arg = idx % num_vectors;
                int arg_idx = idx / num_vectors;
                internal_assert(arg_idx <= (int)vector_component_ids[arg].size());
                result_component_ids[i] = vector_component_ids[arg][arg_idx];
            }

            SpvId result_id = join_vector(op->type, result_component_ids);
            builder.update_id(result_id);
        }
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const VectorReduce *) {
    internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const VectorReduce *): VectorReduce not implemented for codegen\n";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Prefetch *) {
    internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Prefetch *): Prefetch not implemented for codegen\n";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Fork *) {
    internal_error << "void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Fork *): Fork not implemented for codegen";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Acquire *) {
    internal_error << "void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Acquire *): Acquire not implemented for codegen";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Atomic *) {
    internal_error << "void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Atomic *): Atomic not implemented for codegen";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit_unary_op(SpvOp op_code, Type t, const Expr &a) {
    SpvId type_id = builder.declare_type(t);
    a.accept(this);
    SpvId src_a_id = builder.current_id();

    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::unary_op(op_code, type_id, result_id, src_a_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit_binary_op(SpvOp op_code, Type t, const Expr &a, const Expr &b) {
    SpvId type_id = builder.declare_type(t);
    a.accept(this);
    SpvId src_a_id = builder.current_id();
    b.accept(this);
    SpvId src_b_id = builder.current_id();

    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::binary_op(op_code, type_id, result_id, src_a_id, src_b_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit_glsl_op(SpvId glsl_op_code, Type type, const std::vector<Expr> &args) {
    uint32_t type_id = builder.declare_type(type);

    SpvFactory::Operands operands;
    operands.reserve(args.size());
    for (const Expr &e : args) {
        e.accept(this);
        operands.push_back(builder.current_id());
    }

    // sanity check the expected number of operands
    internal_assert(glsl_operand_count(glsl_op_code) == operands.size());

    SpvId inst_set_id = builder.import_glsl_intrinsics();
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::extended(inst_set_id, glsl_op_code, type_id, result_id, operands));
    builder.update_id(result_id);
}

SpvFactory::Components CodeGen_Vulkan_Dev::SPIRV_Emitter::split_vector(Type type, SpvId value_id) {
    SpvFactory::Components value_components;
    SpvId scalar_value_type_id = builder.declare_type(type.with_lanes(1));
    for (uint32_t i = 0; i < (uint32_t)type.lanes(); i++) {
        SpvFactory::Indices extract_indices = {i};
        SpvId value_component_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::composite_extract(scalar_value_type_id, value_component_id, value_id, extract_indices));
        value_components.push_back(value_component_id);
    }
    return value_components;
}

SpvId CodeGen_Vulkan_Dev::SPIRV_Emitter::join_vector(Type type, const SpvFactory::Components &value_components) {
    SpvId type_id = builder.declare_type(type);
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::composite_construct(type_id, result_id, value_components));
    return result_id;
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::init_module() {

    builder.reset();

    // NOTE: Source language is irrelevant. We encode the binary directly
    builder.set_source_language(SpvSourceLanguageUnknown);

    // TODO: Should we autodetect and/or force 32bit or 64bit?
    builder.set_addressing_model(SpvAddressingModelLogical);

    // TODO: Should we autodetect the VulkanMemoryModel extension and use that instead?
    builder.set_memory_model(SpvMemoryModelGLSL450);

    // NOTE: Execution model for Vulkan must be GLCompute which requires Shader support
    builder.require_capability(SpvCapabilityShader);

    // NOTE: Extensions are handled in finalize
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::encode_header(SpvBinary &spirv_header) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::encode_header\n";

    // Encode a sidecar for the module that lists the descriptor sets
    // corresponding to each entry point contained in the module
    //
    // [0] Header word count (total length of header)
    // [1] Number of descriptor sets
    // ... For each descriptor set ...
    // ... [0] Number of uniform buffers for this descriptor set
    // ... [1] Number of storage buffers for this descriptor set
    // ... [2] Length of entry point name (padded to nearest word size)
    // ... [X] Entry point string data
    //

    // NOTE: The Vulkan runtime consumes this header prior to compiling.
    //
    // Both vk_decode_entry_point_data() and vk_compile_shader_module() will
    // need to be updated if the header encoding ever changes!
    //
    uint32_t index = 0;
    spirv_header.push_back(descriptor_set_table.size());
    for (const DescriptorSet &ds : descriptor_set_table) {
        std::vector<char> padded_name;
        uint32_t padded_word_count = (ds.entry_point_name.length() + 3) / 4;
        uint32_t padded_str_length = padded_word_count * 4;
        padded_name.reserve(padded_str_length);
        padded_name.insert(padded_name.begin(), ds.entry_point_name.c_str(), (ds.entry_point_name.c_str() + ds.entry_point_name.length()));
        uint32_t padding = (padded_str_length - ds.entry_point_name.length());
        for (uint32_t i = 0; i < padding; ++i) {
            padded_name.push_back('\0');
        }

        debug(2) << "    [" << index << "] "
                 << "uniform_buffer_count=" << ds.uniform_buffer_count << " "
                 << "storage_buffer_count=" << ds.storage_buffer_count << " "
                 << "entry_point_name_length=" << padded_str_length << " "
                 << "entry_point_name_size=" << padded_name.size() << " "
                 << "entry_point_name: " << (const char *)padded_name.data() << "\n";

        spirv_header.push_back(ds.uniform_buffer_count);
        spirv_header.push_back(ds.storage_buffer_count);
        spirv_header.push_back(padded_str_length);
        internal_assert(padded_name.size() == padded_str_length);
        spirv_header.insert(spirv_header.end(), (const uint32_t *)padded_name.data(), (const uint32_t *)(padded_name.data() + padded_name.size()));
        ++index;
    }
    uint32_t header_word_count = spirv_header.size();
    spirv_header.insert(spirv_header.begin(), header_word_count + 1);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::reset_workgroup_size() {
    workgroup_size[0] = 0;
    workgroup_size[1] = 0;
    workgroup_size[2] = 0;
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::declare_workgroup_size(SpvId kernel_func_id) {
    workgroup_size[0] = std::max(workgroup_size[0], (uint32_t)1);
    workgroup_size[1] = std::max(workgroup_size[1], (uint32_t)1);
    workgroup_size[2] = std::max(workgroup_size[2], (uint32_t)1);

    // Add workgroup size to execution mode
    SpvInstruction exec_mode_inst = SpvFactory::exec_mode_local_size(kernel_func_id, workgroup_size[0], workgroup_size[1], workgroup_size[2]);
    builder.current_module().add_execution_mode(exec_mode_inst);
}

namespace {

// Locate all the unique GPU variables used as SIMT intrinsics
class FindIntrinsicsUsed : public IRVisitor {
    using IRVisitor::visit;
    void visit(const For *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            auto intrinsic = simt_intrinsic(op->name);
            intrinsics_used.insert(intrinsic.first);
        }
        op->body.accept(this);
    }
    void visit(const Variable *op) override {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            auto intrinsic = simt_intrinsic(op->name);
            intrinsics_used.insert(intrinsic.first);
        }
    }

public:
    std::unordered_set<std::string> intrinsics_used;
    FindIntrinsicsUsed() = default;
};

// Map the SPIR-V builtin intrinsic name to its corresponding enum value
SpvBuiltIn map_simt_builtin(const std::string &intrinsic_name) {
    if (starts_with(intrinsic_name, "Workgroup")) {
        return SpvBuiltInWorkgroupId;
    } else if (starts_with(intrinsic_name, "Local")) {
        return SpvBuiltInLocalInvocationId;
    }
    internal_error << "map_simt_builtin called on bad variable name: " << intrinsic_name << "\n";
    return SpvBuiltInMax;
}

}  // namespace

void CodeGen_Vulkan_Dev::SPIRV_Emitter::declare_entry_point(const Stmt &s, SpvId kernel_func_id) {

    // Locate all simt intrinsics
    FindIntrinsicsUsed find_intrinsics;
    s.accept(&find_intrinsics);

    SpvFactory::Variables entry_point_variables;
    for (const std::string &intrinsic_name : find_intrinsics.intrinsics_used) {

        // The builtins are pointers to vec3
        SpvId intrinsic_type_id = builder.declare_type(Type(Type::UInt, 32, 3));
        SpvId intrinsic_ptr_type_id = builder.declare_pointer_type(intrinsic_type_id, SpvStorageClassInput);
        SpvId intrinsic_id = builder.declare_global_variable(intrinsic_name, intrinsic_ptr_type_id, SpvStorageClassInput);
        SpvId intrinsic_loaded_id = builder.reserve_id();
        builder.append(SpvFactory::load(intrinsic_type_id, intrinsic_loaded_id, intrinsic_id));
        symbol_table.push(intrinsic_name, {intrinsic_loaded_id, SpvStorageClassInput});

        // Annotate that this is the specific builtin
        SpvBuiltIn built_in_kind = map_simt_builtin(intrinsic_name);
        SpvBuilder::Literals annotation_literals = {(uint32_t)built_in_kind};
        builder.add_annotation(intrinsic_id, SpvDecorationBuiltIn, annotation_literals);

        // Add the builtin to the interface
        entry_point_variables.push_back(intrinsic_id);
    }

    // Add the entry point with the appropriate execution model
    // NOTE: exec_model must be GLCompute to work with Vulkan ... Kernel is only supported in OpenCL
    builder.add_entry_point(kernel_func_id, SpvExecutionModelGLCompute, entry_point_variables);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::declare_device_args(const Stmt &s, uint32_t entry_point_index,
                                                            const std::string &entry_point_name,
                                                            const std::vector<DeviceArgument> &args) {

    // Keep track of the descriptor set needed to bind this kernel's inputs / outputs
    DescriptorSet descriptor_set;
    descriptor_set.entry_point_name = entry_point_name;

    // Add required extension support for storage types
    for (const auto &arg : args) {
        if (arg.is_buffer) {
            if (arg.type.is_int_or_uint()) {
                if (arg.type.bits() == 8) {
                    builder.require_extension("SPV_KHR_8bit_storage");
                } else if (arg.type.bits() == 16) {
                    builder.require_extension("SPV_KHR_16bit_storage");
                }
            }
        }
    }

    // GLSL-style: each input buffer is a runtime array in a buffer struct
    // All other params get passed in as a single uniform block
    // First, need to count scalar parameters to construct the uniform struct
    SpvBuilder::StructMemberTypes param_struct_members;
    for (const auto &arg : args) {
        if (!arg.is_buffer) {
            SpvId arg_type_id = builder.declare_type(arg.type);
            param_struct_members.push_back(arg_type_id);
        }
    }

    // Add a binding for a uniform buffer packed with all scalar args
    uint32_t binding_counter = 0;
    if (!param_struct_members.empty()) {
        const std::string struct_name = std::string("_struct") + entry_point_name + std::string("_args");
        SpvId param_struct_type_id = builder.declare_struct(struct_name, param_struct_members);

        // Add a decoration describing the offset for each parameter struct member
        uint32_t param_member_index = 0;
        uint32_t param_member_offset = 0;
        for (const auto &arg : args) {
            if (!arg.is_buffer) {
                SpvBuilder::Literals param_offset_literals = {param_member_offset};
                builder.add_struct_annotation(param_struct_type_id, param_member_index, SpvDecorationOffset, param_offset_literals);
                param_member_offset += arg.type.bytes();
                param_member_index++;
            }
        }

        // Add a Block decoration for the parameter pack itself
        builder.add_annotation(param_struct_type_id, SpvDecorationBlock);

        // Add a variable for the parameter pack
        const std::string param_pack_var_name = std::string("_var") + entry_point_name + std::string("_args");
        SpvId param_pack_ptr_type_id = builder.declare_pointer_type(param_struct_type_id, SpvStorageClassUniform);
        SpvId param_pack_var_id = builder.declare_global_variable(param_pack_var_name, param_pack_ptr_type_id, SpvStorageClassUniform);

        // We always pass in the parameter pack as the first binding
        SpvBuilder::Literals binding_index = {0};
        SpvBuilder::Literals dset_index = {entry_point_index};
        builder.add_annotation(param_pack_var_id, SpvDecorationDescriptorSet, dset_index);
        builder.add_annotation(param_pack_var_id, SpvDecorationBinding, binding_index);
        descriptor_set.uniform_buffer_count++;
        binding_counter++;

        // Declare all the args with appropriate offsets into the parameter struct
        uint32_t scalar_index = 0;
        for (const auto &arg : args) {
            if (!arg.is_buffer) {

                SpvId arg_type_id = builder.declare_type(arg.type);
                SpvId access_index_id = builder.declare_constant(UInt(32), &scalar_index);
                SpvId pointer_type_id = builder.declare_pointer_type(arg_type_id, SpvStorageClassUniform);
                SpvFactory::Indices access_indices = {access_index_id};
                SpvId access_chain_id = builder.declare_access_chain(pointer_type_id, param_pack_var_id, access_indices);
                scalar_index++;

                SpvId param_id = builder.reserve_id(SpvResultId);
                builder.append(SpvFactory::load(arg_type_id, param_id, access_chain_id));
                symbol_table.push(arg.name, {param_id, SpvStorageClassUniform});
            }
        }
    }

    // Add bindings for all device buffers declared as GLSL-style buffer blocks in uniform storage
    for (const auto &arg : args) {
        if (arg.is_buffer) {

            // Check for dense loads & stores to determine the widest vector
            // width we can safely index
            CheckAlignedDenseVectorLoadStore check_dense(arg.name);
            s.accept(&check_dense);
            int lanes = check_dense.are_all_dense ? check_dense.lanes : 1;

            // Declare the runtime array (which maps directly to the Halide device buffer)
            Type array_element_type = arg.type.with_lanes(lanes);
            SpvId array_element_type_id = builder.declare_type(array_element_type);
            SpvId runtime_arr_type_id = builder.add_runtime_array(array_element_type_id);

            // Annotate the array with its stride
            SpvBuilder::Literals array_stride = {(uint32_t)(arg.type.bytes())};
            builder.add_annotation(runtime_arr_type_id, SpvDecorationArrayStride, array_stride);

            // Wrap the runtime array in a struct (required with SPIR-V buffer block semantics)
            SpvBuilder::StructMemberTypes struct_member_types = {runtime_arr_type_id};
            const std::string struct_name = std::string("_struct") + entry_point_name + std::string("_b") + std::to_string(binding_counter);
            SpvId struct_type_id = builder.declare_struct(struct_name, struct_member_types);

            // Declare a pointer to the struct as a global variable
            SpvStorageClass storage_class = SpvStorageClassUniform;
            SpvId ptr_struct_type_id = builder.declare_pointer_type(struct_type_id, storage_class);
            SpvId buffer_block_var_id = builder.declare_global_variable(arg.name, ptr_struct_type_id, storage_class);

            // Annotate the struct to indicate it's passed in a GLSL-style buffer block
            builder.add_annotation(struct_type_id, SpvDecorationBufferBlock);

            // Annotate the offset for the array
            SpvBuilder::Literals zero_literal = {uint32_t(0)};
            builder.add_struct_annotation(struct_type_id, 0, SpvDecorationOffset, zero_literal);

            // Set descriptor set and binding indices
            SpvBuilder::Literals dset_index = {entry_point_index};
            SpvBuilder::Literals binding_index = {uint32_t(binding_counter++)};
            builder.add_annotation(buffer_block_var_id, SpvDecorationDescriptorSet, dset_index);
            builder.add_annotation(buffer_block_var_id, SpvDecorationBinding, binding_index);
            symbol_table.push(arg.name, {buffer_block_var_id, storage_class});

            StorageAccess access;
            access.storage_type_id = array_element_type_id;
            access.storage_type = array_element_type;
            access.storage_class = storage_class;
            storage_access_map[buffer_block_var_id] = access;
            descriptor_set.storage_buffer_count++;
        }
    }

    // Save the descriptor set (so we can output the binding information as a header to the code module)
    descriptor_set_table.push_back(descriptor_set);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::compile(std::vector<char> &module) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::compile\n";

    // First encode the descriptor set bindings for each entry point
    // as a sidecar which we will add as a preamble header to the actual
    // SPIR-V binary so the runtime can know which descriptor set to use
    // for each entry point
    SpvBinary spirv_header;
    encode_header(spirv_header);

    // Finalize and encode the SPIR-V IR into a compliant binary
    SpvBinary spirv_binary;
    builder.finalize();
    builder.encode(spirv_binary);

    size_t header_bytes = spirv_header.size() * sizeof(uint32_t);
    size_t binary_bytes = spirv_binary.size() * sizeof(uint32_t);

    debug(2) << "    encoding module ("
             << "header_size: " << (uint32_t)(header_bytes) << ", "
             << "binary_size: " << (uint32_t)(binary_bytes) << ")\n";

    // Combine the header and binary into the module
    module.reserve(header_bytes + binary_bytes);
    module.insert(module.end(), (const char *)spirv_header.data(), (const char *)(spirv_header.data() + spirv_header.size()));
    module.insert(module.end(), (const char *)spirv_binary.data(), (const char *)(spirv_binary.data() + spirv_binary.size()));
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::add_kernel(const Stmt &s,
                                                   const std::string &name,
                                                   const std::vector<DeviceArgument> &args) {
    debug(2) << "Adding Vulkan kernel " << name << "\n";

    // Add function definition
    // TODO: can we use one of the function control annotations?

    // We'll discover the workgroup size as we traverse the kernel
    reset_workgroup_size();

    // Declare the kernel function
    SpvId void_type_id = builder.declare_void_type();
    SpvId kernel_func_id = builder.add_function(name, void_type_id);
    SpvFunction kernel_func = builder.lookup_function(kernel_func_id);
    uint32_t entry_point_index = builder.current_module().entry_point_count();
    builder.enter_function(kernel_func);

    // Declare the entry point and input intrinsics for the kernel func
    declare_entry_point(s, kernel_func_id);

    // Declare all parameters -- scalar args and device buffers
    declare_device_args(s, entry_point_index, name, args);

    // Traverse
    s.accept(this);

    // Insert return statement end delimiter
    kernel_func.tail_block().add_instruction(SpvFactory::return_stmt());

    // Declare the workgroup size now that we've traversed the kernel
    declare_workgroup_size(kernel_func_id);

    // Pop scope
    for (const auto &arg : args) {
        symbol_table.pop(arg.name);
    }
    builder.leave_block();
    builder.leave_function();
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::dump() const {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::dump()\n";
    std::cerr << builder.current_module();
}

CodeGen_Vulkan_Dev::CodeGen_Vulkan_Dev(Target t) {
}

void CodeGen_Vulkan_Dev::init_module() {
    debug(2) << "CodeGen_Vulkan_Dev::init_module\n";
    emitter.init_module();
}

void CodeGen_Vulkan_Dev::add_kernel(Stmt stmt,
                                    const std::string &name,
                                    const std::vector<DeviceArgument> &args) {

    debug(2) << "CodeGen_Vulkan_Dev::add_kernel " << name << "\n";

    // We need to scalarize/de-predicate any loads/stores, since Vulkan does not support predication.
    stmt = scalarize_predicated_loads_stores(stmt);

    debug(2) << "CodeGen_Vulkan_Dev: after removing predication: \n"
             << stmt;

    current_kernel_name = name;
    emitter.add_kernel(stmt, name, args);

    // dump the SPIRV file if requested
    if (getenv("HL_SPIRV_DUMP_FILE")) {
        dump();
    }
}

std::vector<char> CodeGen_Vulkan_Dev::compile_to_src() {
    debug(2) << "CodeGen_Vulkan_Dev::compile_to_src\n";
    std::vector<char> module;
    emitter.compile(module);
    return module;
}

std::string CodeGen_Vulkan_Dev::get_current_kernel_name() {
    return current_kernel_name;
}

std::string CodeGen_Vulkan_Dev::print_gpu_name(const std::string &name) {
    return name;
}

void CodeGen_Vulkan_Dev::dump() {
    std::vector<char> module = compile_to_src();

    // Print the contents of the compiled SPIR-V module
    emitter.dump();

    // Skip the header and only output the SPIR-V binary
    const uint32_t *decode = (const uint32_t *)(module.data());
    uint32_t header_word_count = decode[0];
    size_t header_size = header_word_count * sizeof(uint32_t);
    const uint32_t *binary_ptr = (decode + header_word_count);
    size_t binary_size = (module.size() - header_size);

    const char *filename = getenv("HL_SPIRV_DUMP_FILE") ? getenv("HL_SPIRV_DUMP_FILE") : "out.spv";
    debug(1) << "Vulkan: Dumping SPIRV module to file: '" << filename << "'\n";
    std::ofstream f(filename, std::ios::out | std::ios::binary);
    f.write((const char *)(binary_ptr), binary_size);
    f.close();
}

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_Vulkan_Dev(const Target &target) {
    return std::make_unique<CodeGen_Vulkan_Dev>(target);
}

}  // namespace Internal
}  // namespace Halide

#else  // WITH_SPIRV

namespace Halide {
namespace Internal {

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_Vulkan_Dev(const Target &target) {
    return nullptr;
}

}  // namespace Internal
}  // namespace Halide

#endif  // WITH_SPIRV

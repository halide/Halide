#include <algorithm>
#include <sstream>

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Vulkan_Dev.h"
#include "Debug.h"
#include "Deinterleave.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Scope.h"
#include "SpirvIR.h"
#include "Target.h"

// Temporary:
#include <fstream>

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

        void visit(const Variable *) override;
        void visit(const IntImm *) override;
        void visit(const UIntImm *) override;
        void visit(const StringImm *) override;
        void visit(const FloatImm *) override;
        void visit(const Cast *) override;
        void visit(const Add *) override;
        void visit(const Sub *) override;
        void visit(const Mul *) override;
        void visit(const Div *) override;
        void visit(const Mod *) override;
        void visit(const Max *) override;
        void visit(const Min *) override;
        void visit(const EQ *) override;
        void visit(const NE *) override;
        void visit(const LT *) override;
        void visit(const LE *) override;
        void visit(const GT *) override;
        void visit(const GE *) override;
        void visit(const And *) override;
        void visit(const Or *) override;
        void visit(const Not *) override;
        void visit(const Call *) override;
        void visit(const Select *) override;
        void visit(const Load *) override;
        void visit(const Store *) override;
        void visit(const Let *) override;
        void visit(const LetStmt *) override;
        void visit(const AssertStmt *) override;
        void visit(const ProducerConsumer *) override;
        void visit(const For *) override;
        void visit(const Ramp *) override;
        void visit(const Broadcast *) override;
        void visit(const Provide *) override;
        void visit(const Allocate *) override;
        void visit(const Free *) override;
        void visit(const Realize *) override;
        void visit(const IfThenElse *) override;
        void visit(const Evaluate *) override;
        void visit(const Shuffle *) override;
        void visit(const Prefetch *) override;
        void visit(const Fork *) override;
        void visit(const Acquire *) override;

        void visit_binop(Type t, const Expr &a, const Expr &b, SpvOp op_code);

        // The SPIRV-IR builder
        SpvBuilder builder;

        // Top-level function for adding kernels
        void add_kernel(const Stmt &s, const std::string &name, const std::vector<DeviceArgument> &args);
        void init_module();
        void compile(std::vector<char> &binary);

        // Scalarize expressions
        void scalarize(const Expr &e);
        SpvId map_type_to_pair(const Type &t);

        // The scope contains both the symbol id and its storage class
        using SymbolIdStorageClassPair = std::pair<SpvId, SpvStorageClass>;
        using SymbolScope = Scope<SymbolIdStorageClassPair>;
        using ScopedSymbolBinding = ScopedBinding<SymbolIdStorageClassPair>;
        SymbolScope symbol_table;

        // The workgroup size.  Must be the same for all kernels.
        uint32_t workgroup_size[3];

        // Returns Phi node inputs.
        template<typename StmtOrExpr>
        SpvFactory::BlockVariables emit_if_then_else(const Expr &condition, StmtOrExpr then_case, StmtOrExpr else_case);

    } emitter;

    std::string current_kernel_name;
};

void CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize(const Expr &e) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize(): " << (Expr)e << "\n";
    internal_assert(e.type().is_vector()) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::scalarize must be called with an expression of vector type.\n";

    SpvId type_id = builder.declare_type(e.type());
    SpvId value_id = builder.declare_null_constant(e.type());
    SpvId result_id = value_id;
    for (int i = 0; i < e.type().lanes(); i++) {
        extract_lane(e, i).accept(this);
        SpvId vector_id = builder.current_id();
        SpvId composite_vector_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::vector_insert_dynamic(type_id, composite_vector_id, vector_id, value_id, i));
        result_id = composite_vector_id;
    }
    builder.update_id(result_id);
}

SpvId CodeGen_Vulkan_Dev::SPIRV_Emitter::map_type_to_pair(const Type &t) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::map_type_to_pair(): " << t << "\n";
    SpvId base_type_id = builder.declare_type(t);
    const std::string &type_name = type_to_c_type(t, false, false) + std::string("_pair");
    SpvBuilder::StructMemberTypes member_type_ids = {base_type_id, base_type_id};
    SpvId struct_type_id = builder.declare_struct(type_name, member_type_ids);
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

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Cast *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast): " << op->value.type() << " to " << op->type << "\n";

    SpvOp op_code = SpvOpNop;
    if (op->value.type().is_float()) {
        if (op->type.is_float()) {
            op_code = SpvOpFConvert;
        } else if (op->type.is_uint()) {
            op_code = SpvOpConvertFToU;
        } else if (op->type.is_int()) {
            op_code = SpvOpConvertFToS;
        } else {
            internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << "\n";
        }
    } else if (op->value.type().is_uint()) {
        if (op->type.is_float()) {
            op_code = SpvOpConvertUToF;
        } else if (op->type.is_uint()) {
            op_code = SpvOpUConvert;
        } else if (op->type.is_int()) {
            op_code = SpvOpSatConvertUToS;
        } else {
            internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << "\n";
        }
    } else if (op->value.type().is_int()) {
        if (op->type.is_float()) {
            op_code = SpvOpConvertSToF;
        } else if (op->type.is_uint()) {
            op_code = SpvOpSatConvertSToU;
        } else if (op->type.is_int()) {
            op_code = SpvOpSConvert;
        } else {
            internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << "\n";
        }
    } else {
        internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Cast):  unhandled case " << op->value.type() << " to " << op->type << "\n";
    }

    SpvId type_id = builder.declare_type(op->type);
    op->value.accept(this);
    SpvId src_id = builder.current_id();
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::convert(op_code, type_id, result_id, src_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Add *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Add): " << op->type << " ((" << op->a << ") + (" << op->b << "))\n";
    visit_binop(op->type, op->a, op->b, op->type.is_float() ? SpvOpFAdd : SpvOpIAdd);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Sub *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Sub): " << op->type << " ((" << op->a << ") - (" << op->b << "))\n";
    visit_binop(op->type, op->a, op->b, op->type.is_float() ? SpvOpFSub : SpvOpISub);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Mul *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Mul): " << op->type << " ((" << op->a << ") * (" << op->b << "))\n";
    visit_binop(op->type, op->a, op->b, op->type.is_float() ? SpvOpFMul : SpvOpIMul);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Div *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Div): " << op->type << " ((" << op->a << ") / (" << op->b << "))\n";
    user_assert(!is_const_zero(op->b)) << "Division by constant zero in expression: " << Expr(op) << "\n";

    if (op->type.is_float()) {
        visit_binop(op->type, op->a, op->b, SpvOpFDiv);
    } else {
        Expr e = lower_int_uint_div(op->a, op->b);
        e.accept(this);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Mod *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Mod): " << op->type << " ((" << op->a << ") % (" << op->b << "))\n";
    if (op->type.is_float()) {
        // Takes sign of result from op->b
        visit_binop(op->type, op->a, op->b, SpvOpFMod);
    } else {
        Expr e = lower_int_uint_mod(op->a, op->b);
        e.accept(this);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Max *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Max): " << op->type << " Max((" << op->a << "), (" << op->b << "))\n";

    std::string a_name = unique_name('a');
    std::string b_name = unique_name('b');
    Expr a = Variable::make(op->a.type(), a_name);
    Expr b = Variable::make(op->b.type(), b_name);
    Expr temp = Let::make(a_name, op->a,
                          Let::make(b_name, op->b, select(a > b, a, b)));
    temp.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Min *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Min): " << op->type << " Min((" << op->a << "), (" << op->b << "))\n";
    std::string a_name = unique_name('a');
    std::string b_name = unique_name('b');
    Expr a = Variable::make(op->a.type(), a_name);
    Expr b = Variable::make(op->b.type(), b_name);
    Expr temp = Let::make(a_name, op->a,
                          Let::make(b_name, op->b, select(a < b, a, b)));
    temp.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const EQ *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(EQ): " << op->type << " (" << op->a << ") == (" << op->b << ")\n";
    visit_binop(op->type, op->a, op->b, op->type.is_float() ? SpvOpFOrdEqual : SpvOpIEqual);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const NE *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(NE): " << op->type << " (" << op->a << ") != (" << op->b << ")\n";
    visit_binop(op->type, op->a, op->b, op->type.is_float() ? SpvOpFOrdNotEqual : SpvOpINotEqual);
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
    visit_binop(op->type, op->a, op->b, op_code);
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
    visit_binop(op->type, op->a, op->b, op_code);
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
    visit_binop(op->type, op->a, op->b, op_code);
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
    visit_binop(op->type, op->a, op->b, op_code);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const And *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(And): " << op->type << " (" << op->a << ") && (" << op->b << ")\n";
    visit_binop(op->type, op->a, op->b, SpvOpLogicalAnd);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Or *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Or): " << op->type << " (" << op->a << ") || (" << op->b << ")\n";
    visit_binop(op->type, op->a, op->b, SpvOpLogicalOr);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Not *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Not): " << op->type << " !(" << op->a << ")\n";

    SpvId type_id = builder.declare_type(op->type);
    op->a.accept(this);
    SpvId src_id = builder.current_id();
    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::logical_not(type_id, result_id, src_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Call *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Call): " << op->type << " " << op->name << " args=" << (uint32_t)op->args.size() << "\n";

    if (op->is_intrinsic(Call::gpu_thread_barrier)) {
        // TODO: Check the scopes here and figure out if this is the
        // right memory barrier. Might be able to use
        // SpvMemorySemanticsMaskNone instead.
        SpvId current_function_id = builder.current_function().id();
        builder.append(SpvFactory::control_barrier(current_function_id, current_function_id,
                                                   SpvMemorySemanticsAcquireReleaseMask));
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], SpvOpBitwiseAnd);
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], SpvOpBitwiseXor);
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        visit_binop(op->type, op->args[0], op->args[1], SpvOpBitwiseOr);
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        SpvId type_id = builder.declare_type(op->type);
        op->args[0]->accept(this);
        SpvId arg_id = builder.current_id();
        SpvId result_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::logical_not(type_id, result_id, arg_id));
        builder.update_id(result_id);
    } else if (op->is_intrinsic(Call::if_then_else)) {
        if (op->type.is_vector()) {
            scalarize(op);
        } else {
            // Generate Phi node if used as an expression.
            internal_assert(op->args.size() == 3);
            SpvFactory::BlockVariables block_vars = emit_if_then_else(op->args[0], op->args[1], op->args[2]);
            SpvId type_id = builder.declare_type(op->type);
            SpvId result_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::phi(type_id, result_id, block_vars));
            builder.update_id(result_id);
        }
    } else if (op->is_intrinsic(Call::IntrinsicOp::div_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        SpvOp op_code = SpvOpNop;
        if (op->type.is_int()) {
            op_code = SpvOpSDiv;
        } else if (op->type.is_uint()) {
            op_code = SpvOpUDiv;
        } else {
            internal_error << "div_round_to_zero of non-integer type.\n";
        }
        visit_binop(op->type, op->args[0], op->args[1], op_code);
    } else if (op->is_intrinsic(Call::IntrinsicOp::mod_round_to_zero)) {
        internal_assert(op->args.size() == 2);
        SpvOp op_code = SpvOpNop;
        if (op->type.is_int()) {
            op_code = SpvOpSMod;
        } else if (op->type.is_uint()) {
            op_code = SpvOpUMod;
        } else {
            internal_error << "mod_round_to_zero of non-integer type.\n";
        }
        visit_binop(op->type, op->args[0], op->args[1], op_code);
    } else if (op->is_intrinsic(Call::IntrinsicOp::mul_shift_right)) {
        internal_assert(op->args.size() == 3);
        uint32_t type_id = builder.declare_type(op->type);

        op->args[0].accept(this);
        SpvId src_a_id = builder.current_id();
        op->args[1].accept(this);
        SpvId src_b_id = builder.current_id();

        SpvId pair_type_id = map_type_to_pair(op->type);

        // Double width multiply
        SpvId product_pair_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::multiply_extended(pair_type_id, product_pair_id, src_a_id, src_b_id, op->type.is_uint() ? false : true));

        SpvFactory::Indices indices = {1};
        uint32_t high_item_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::composite_extract(type_id, high_item_id, product_pair_id, indices));

        const UIntImm *shift = op->args[2].as<UIntImm>();
        internal_assert(shift != nullptr) << "Third argument to mul_shift_right intrinsic must be an unsigned integer immediate.\n";

        SpvId result_id = high_item_id;
        if (shift->value != 0) {
            // TODO: This code depends on compilation happening on a little-endian host.
            SpvId shift_amount_id = builder.declare_constant(shift->type, &shift->value);
            result_id = builder.reserve_id(SpvResultId);
            if (op->type.is_uint()) {
                builder.append(SpvFactory::shift_right_logical(type_id, result_id, high_item_id, shift_amount_id));
            } else {
                builder.append(SpvFactory::shift_right_arithmetic(type_id, result_id, high_item_id, shift_amount_id));
            }
        }
        builder.update_id(result_id);
    } else if (op->is_intrinsic(Call::IntrinsicOp::sorted_avg)) {
        internal_assert(op->args.size() == 2);
        // b > a, so the following works without widening:
        // a + (b - a)/2
        Expr e = op->args[0] + (op->args[1] - op->args[0]) / 2;
        e.accept(this);
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

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Load *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Load): " << (Expr)op << "\n";
    user_assert(is_const_one(op->predicate)) << "Predicated loads not supported by SPIR-V codegen\n";

    // TODO: implement vector loads
    // TODO: correct casting to the appropriate memory space

    internal_assert(!(op->index.type().is_vector()));
    internal_assert(op->param.defined() && op->param.is_buffer());

    // Construct the pointer to read from
    internal_assert(symbol_table.contains(op->name));
    SymbolIdStorageClassPair id_and_storage_class = symbol_table.get(op->name);
    SpvId base_id = id_and_storage_class.first;
    SpvStorageClass storage_class = id_and_storage_class.second;
    internal_assert(base_id != SpvInvalidId);
    internal_assert(((uint32_t)storage_class) < ((uint32_t)SpvStorageClassMax));

    op->index.accept(this);
    SpvId index_id = builder.current_id();

    uint32_t zero = 0;
    SpvId type_id = builder.declare_type(op->type);
    SpvId zero_id = builder.declare_constant(UInt(32), &zero);
    SpvId ptr_type_id = builder.declare_pointer_type(type_id, storage_class);
    SpvId access_chain_id = builder.reserve_id(SpvResultId);
    SpvFactory::Indices indices = {index_id};
    builder.append(SpvFactory::in_bounds_access_chain(ptr_type_id, access_chain_id, base_id, zero_id, indices));

    SpvId result_id = builder.reserve_id(SpvResultId);
    SpvId result_type_id = builder.declare_type(op->type);
    builder.append(SpvFactory::load(result_type_id, result_id, access_chain_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Store *op) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(Store): " << op->name << "[" << op->index << "] = (" << op->value << ")\n";
    user_assert(is_const_one(op->predicate)) << "Predicated stores not supported by SPIR-V codegen!\n";

    // TODO: implement vector writes
    // TODO: correct casting to the appropriate memory space

    internal_assert(!(op->index.type().is_vector()));
    internal_assert(op->param.defined() && op->param.is_buffer());

    op->value.accept(this);
    SpvId value_id = builder.current_id();

    // Construct the pointer to write to
    internal_assert(symbol_table.contains(op->name));
    SymbolIdStorageClassPair id_and_storage_class = symbol_table.get(op->name);
    SpvId base_id = id_and_storage_class.first;
    SpvStorageClass storage_class = id_and_storage_class.second;
    internal_assert(base_id != SpvInvalidId);
    internal_assert(((uint32_t)storage_class) < ((uint32_t)SpvStorageClassMax));

    op->index.accept(this);
    SpvId index_id = builder.current_id();
    SpvId type_id = builder.declare_type(op->value.type());
    SpvId ptr_type_id = builder.declare_pointer_type(type_id, storage_class);
    SpvId access_chain_id = builder.reserve_id(SpvResultId);

    SpvId zero = 0;
    SpvId zero_id = builder.declare_constant(UInt(32), &zero);
    SpvFactory::Indices indices = {index_id};
    builder.append(SpvFactory::in_bounds_access_chain(ptr_type_id, access_chain_id, base_id, zero_id, indices));
    builder.append(SpvFactory::store(access_chain_id, value_id));
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

    // TODO: Figure out undef here?
    builder.update_id(SpvInvalidId);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const AssertStmt *) {
    // TODO: Fill this in.
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const ProducerConsumer *) {
    // I believe these nodes are solely for annotation purposes.
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

        internal_assert(op->for_type == ForType::Serial) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit unhandled For type: " << op->for_type << "\n";

        // TODO: Loop vars are alway int32_t right?
        SpvId index_type_id = builder.declare_type(Int(32));
        SpvId index_var_type_id = builder.declare_pointer_type(index_type_id, SpvStorageClassFunction);

        op->min.accept(this);
        SpvId min_id = builder.current_id();
        op->extent.accept(this);
        SpvId extent_id = builder.current_id();

        // Compute max.
        SpvId max_id = builder.reserve_id(SpvResultId);
        builder.append(SpvFactory::integer_add(index_type_id, max_id, min_id, extent_id));

        // Declare loop var
        SpvId loop_var_id = builder.declare_variable(unique_name("loop_index"), index_var_type_id, SpvStorageClassFunction, min_id);

        SpvId header_block_id = builder.reserve_id(SpvBlockId);
        SpvId top_block_id = builder.reserve_id(SpvBlockId);
        SpvId body_block_id = builder.reserve_id(SpvBlockId);
        SpvId continue_block_id = builder.reserve_id(SpvBlockId);
        SpvId merge_block_id = builder.reserve_id(SpvBlockId);

        SpvBlock header_block = builder.create_block(header_block_id);
        builder.enter_block(header_block);
        {
            builder.append(SpvFactory::loop_merge(merge_block_id, continue_block_id, SpvLoopControlMaskNone));
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
            builder.append(SpvFactory::less_than_equal(loop_test_type_id, loop_test_id, current_index_id, max_id, true));
            builder.append(SpvFactory::conditional_branch(loop_test_id, body_block_id, merge_block_id));
        }
        builder.leave_block();

        SpvBlock body_block = builder.create_block(body_block_id);
        builder.enter_block(body_block);
        {
            ScopedSymbolBinding binding(symbol_table, op->name, {current_index_id, SpvStorageClassFunction});
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

        SpvBlock merge_block = builder.create_block(merge_block_id);
        builder.enter_block(merge_block);
    }
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Ramp *op) {
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

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Allocate *) {
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Free *) {
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
//    SpvId if_block_id = builder.reserve_id(SpvBlockId);
    SpvId then_block_id = builder.reserve_id(SpvBlockId);
    SpvId else_block_id = else_case.defined() ? builder.reserve_id(SpvBlockId) : merge_block_id;
 
    SpvFactory::BlockVariables block_vars;

    // If Conditional
//    SpvBlock if_block = builder.create_block(if_block_id);
//    builder.enter_block(if_block);
//    {
        builder.append(SpvFactory::selection_merge(merge_block_id, SpvSelectionControlMaskNone));
        builder.append(SpvFactory::conditional_branch(cond_id, then_block_id, else_block_id));
//    }
//    builder.leave_block();

    // Then block
    SpvBlock then_block = builder.create_block(then_block_id);
    builder.enter_block(then_block);
    {
        then_case.accept(this);
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
    emit_if_then_else(op->condition, op->then_case, op->else_case);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Evaluate *op) {
    op->value.accept(this);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Shuffle *op) {
    internal_assert(op->vectors.size() == 2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Shuffle *op): SPIR-V codegen currently only supports shuffles of vector pairs.\n";
    SpvId type_id = builder.declare_type(op->type);
    op->vectors[0].accept(this);
    SpvId vector0_id = builder.current_id();
    op->vectors[1].accept(this);
    SpvId vector1_id = builder.current_id();

    SpvFactory::Indices indices;
    indices.insert(indices.end(), op->indices.begin(), op->indices.end());

    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::vector_shuffle(type_id, result_id, vector0_id, vector1_id, indices));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Prefetch *) {
    internal_error << "CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Prefetch *): Prefetch encountered during codegen\n";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Fork *) {
    internal_error << "void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Fork *) not supported yet.";
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Acquire *) {
    internal_error << "void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit(const Acquire *) not supported yet.";
}

// TODO: fast math decorations.
void CodeGen_Vulkan_Dev::SPIRV_Emitter::visit_binop(Type t, const Expr &a, const Expr &b, SpvOp op_code) {
    SpvId type_id = builder.declare_type(t);
    a.accept(this);
    SpvId src_a_id = builder.current_id();
    b.accept(this);
    SpvId src_b_id = builder.current_id();

    SpvId result_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::binary_op(op_code, type_id, result_id, src_a_id, src_b_id));
    builder.update_id(result_id);
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::init_module() {

    builder.reset();

    // NOTE: Source language is irrelevant. We encode the binary directly
    builder.set_source_language(SpvSourceLanguageUnknown);

    // TODO: Should we autodetect and/or force 32bit or 64bit?
    builder.set_addressing_model(SpvAddressingModelLogical);

    // TODO: Is there a better memory model to use?
    builder.set_memory_model(SpvMemoryModelGLSL450);

    // Capabilities
    builder.require_capability(SpvCapabilityShader);

    // NOTE: Extensions are handled in finalize
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::compile(std::vector<char> &module) {
    debug(2) << "CodeGen_Vulkan_Dev::SPIRV_Emitter::compile\n";
    SpvBinary spirv_binary;
    builder.finalize();
    builder.encode(spirv_binary);
    module.reserve(spirv_binary.size() * sizeof(uint32_t));
    module.insert(module.end(), (const char *)spirv_binary.data(), (const char *)(spirv_binary.data() + spirv_binary.size()));
}

void CodeGen_Vulkan_Dev::SPIRV_Emitter::add_kernel(const Stmt &s,
                                                   const std::string &name,
                                                   const std::vector<DeviceArgument> &args) {
    debug(2) << "Adding Vulkan kernel " << name << "\n";

    // Add function definition
    // TODO: can we use one of the function control annotations?

    // We'll discover the workgroup size as we traverse the kernel
    workgroup_size[0] = 0;
    workgroup_size[1] = 0;
    workgroup_size[2] = 0;

    // Declare the kernel function
    SpvId void_type_id = builder.declare_void_type();
    SpvId kernel_func_id = builder.add_function(name, void_type_id);
    SpvFunction kernel_func = builder.lookup_function(kernel_func_id);
    builder.enter_function(kernel_func);

    // TODO: only add the SIMT intrinsics used
    SpvFactory::Variables entry_point_variables;
    auto intrinsics = {"WorkgroupId", "LocalInvocationId"};
    for (const std::string &intrinsic_name : intrinsics) {

        // The builtins are pointers to vec3
        SpvId intrinsic_type_id = builder.declare_type(Type(Type::UInt, 32, 3));
        SpvId intrinsic_ptr_type_id = builder.declare_pointer_type(intrinsic_type_id, SpvStorageClassInput);
        SpvId intrinsic_id = builder.declare_global_variable(intrinsic_name, intrinsic_ptr_type_id, SpvStorageClassInput);
        SpvId intrinsic_loaded_id = builder.reserve_id();
        builder.append(SpvFactory::load(intrinsic_type_id, intrinsic_loaded_id, intrinsic_id));
        symbol_table.push(intrinsic_name, {intrinsic_loaded_id, SpvStorageClassInput});

        // Annotate that this is the specific builtin
        SpvBuiltIn built_in_kind = starts_with(intrinsic_name, "Workgroup") ? SpvBuiltInWorkgroupId : SpvBuiltInLocalInvocationId;
        SpvBuilder::Literals annotation_literals = {(uint32_t)built_in_kind};
        builder.add_annotation(intrinsic_id, SpvDecorationBuiltIn, annotation_literals);

        // Add the builtin to the interface
        entry_point_variables.push_back(intrinsic_id);
    }

    // Add the entry point and exection mode
    builder.add_entry_point(kernel_func_id, SpvExecutionModelGLCompute, entry_point_variables);

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
    SpvId param_struct_type_id = builder.declare_struct(unique_name("param_struct"), param_struct_members);

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
    SpvId param_pack_ptr_type_id = builder.declare_pointer_type(param_struct_type_id, SpvStorageClassUniform);
    SpvId param_pack_var_id = builder.declare_global_variable(unique_name("kernel_params"), param_pack_ptr_type_id, SpvStorageClassUniform);

    // We always pass in the parameter pack as the first binding
    SpvBuilder::Literals zero_literal = {0};
    builder.add_annotation(param_pack_var_id, SpvDecorationDescriptorSet, zero_literal);
    builder.add_annotation(param_pack_var_id, SpvDecorationBinding, zero_literal);

    uint32_t binding_counter = 1;
    uint32_t scalar_index = 0;
    for (const auto &arg : args) {
        if (arg.is_buffer) {
            SpvId element_type_id = builder.declare_type(arg.type);
            SpvId runtime_arr_type_id = builder.add_runtime_array(element_type_id);
            SpvBuilder::StructMemberTypes struct_member_types = {runtime_arr_type_id};
            SpvId struct_type_id = builder.declare_struct(unique_name("param_buffer_" + std::to_string(binding_counter)), struct_member_types);
            SpvId ptr_struct_type_id = builder.declare_pointer_type(struct_type_id, SpvStorageClassUniform);
            SpvId param_id = builder.declare_global_variable(unique_name("param_" + arg.name), ptr_struct_type_id, SpvStorageClassUniform);

            // Annotate the struct to indicate it's passed in a GLSL-style buffer block
            builder.add_annotation(struct_type_id, SpvDecorationBufferBlock);

            // Annotate the array with its stride
            SpvBuilder::Literals array_stride = {(uint32_t)(arg.type.bytes())};
            builder.add_annotation(runtime_arr_type_id, SpvDecorationArrayStride, array_stride);

            // Annotate the offset for the array
            SpvBuilder::Literals zero_literal = {uint32_t(0)};
            builder.add_struct_annotation(struct_type_id, 0, SpvDecorationOffset, zero_literal);

            // Set DescriptorSet and Binding
            SpvBuilder::Literals binding_index = {uint32_t(binding_counter++)};
            builder.add_annotation(param_id, SpvDecorationDescriptorSet, zero_literal);
            builder.add_annotation(param_id, SpvDecorationBinding, binding_index);
            symbol_table.push(arg.name, {param_id, SpvStorageClassUniform});

        } else {

            SpvId arg_type_id = builder.declare_type(arg.type);
            SpvId access_index_id = builder.declare_constant(UInt(32), &scalar_index);
            SpvId pointer_type_id = builder.declare_pointer_type(arg_type_id, SpvStorageClassUniform);
            SpvId access_chain_id = builder.declare_access_chain(pointer_type_id, param_pack_var_id, access_index_id, {});
            scalar_index++;

            SpvId param_id = builder.reserve_id(SpvResultId);
            builder.append(SpvFactory::load(arg_type_id, param_id, access_chain_id));
            symbol_table.push(arg.name, {param_id, SpvStorageClassUniform});
        }
    }

    s.accept(this);

    // Insert return statement end delimiter
    kernel_func.last_block().add_instruction(SpvFactory::return_stmt());

    workgroup_size[0] = std::max(workgroup_size[0], (uint32_t)1);
    workgroup_size[1] = std::max(workgroup_size[1], (uint32_t)1);
    workgroup_size[2] = std::max(workgroup_size[2], (uint32_t)1);

    // Add workgroup size to execution mode
    SpvInstruction exec_mode_inst = SpvFactory::exec_mode_local_size(kernel_func_id, workgroup_size[0], workgroup_size[1], workgroup_size[2]);
    builder.current_module().add_execution_mode(exec_mode_inst);

    // Pop scope
    for (const auto &arg : args) {
        symbol_table.pop(arg.name);
    }
    builder.leave_block();
    builder.leave_function();
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
    const char *filename = getenv("HL_SPIRV_DUMP_FILE") ? getenv("HL_SPIRV_DUMP_FILE") : "out.spv";
    debug(1) << "Vulkan: Dumping SPIRV module to file: '" << filename << "'\n";
    std::ofstream f(filename, std::ios::out | std::ios::binary);
    f.write((char *)(module.data()), module.size());
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

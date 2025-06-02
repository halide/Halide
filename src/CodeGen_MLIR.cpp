#include <llvm/Support/raw_os_ostream.h>

#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/MemRef/IR/MemRef.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/Vector/IR/VectorOps.h>
#include <mlir/IR/ImplicitLocOpBuilder.h>
#include <mlir/IR/Verifier.h>

#include "CodeGen_MLIR.h"
#include "IROperator.h"
#include "Module.h"

namespace Halide {

namespace Internal {

CodeGen_MLIR::CodeGen_MLIR(std::ostream &stream)
    : stream(stream) {
    mlir_context.loadDialect<mlir::arith::ArithDialect>();
    mlir_context.loadDialect<mlir::func::FuncDialect>();
    mlir_context.loadDialect<mlir::memref::MemRefDialect>();
    mlir_context.loadDialect<mlir::scf::SCFDialect>();
    mlir_context.loadDialect<mlir::vector::VectorDialect>();
}

void CodeGen_MLIR::compile(const Module &module) {
    mlir::LocationAttr loc = mlir::UnknownLoc::get(&mlir_context);
    mlir::ModuleOp mlir_module = mlir::ModuleOp::create(loc, module.name());
    mlir::ImplicitLocOpBuilder builder = mlir::ImplicitLocOpBuilder::atBlockEnd(loc, mlir_module.getBody());

    for (const auto &func : module.functions())
        compile_func(builder, func);

    internal_assert(mlir::verify(mlir_module).succeeded());

    llvm::raw_os_ostream output(stream);
    mlir_module.print(output);
}

void CodeGen_MLIR::compile_func(mlir::ImplicitLocOpBuilder &builder, const LoweredFunc &func) {
    mlir::SmallVector<mlir::Type> inputs;
    mlir::SmallVector<mlir::Type> results;
    mlir::SmallVector<mlir::NamedAttribute> funcAttrs;
    mlir::SmallVector<mlir::DictionaryAttr> funcArgAttrs;

    for (const auto &arg : func.args)
        inputs.push_back(arg.is_buffer() ? mlir::MemRefType::get({0}, mlir_type_of(builder, arg.type)) :
                                           mlir_type_of(builder, arg.type));

    mlir::FunctionType functionType = builder.getFunctionType(inputs, results);
    mlir::func::FuncOp functionOp = builder.create<mlir::func::FuncOp>(builder.getStringAttr(func.name),
                                                                       functionType, funcAttrs, funcArgAttrs);
    builder.setInsertionPointToStart(functionOp.addEntryBlock());

    CodeGen_MLIR::Visitor visitor(builder, func);
    func.body.accept(&visitor);
    builder.create<mlir::func::ReturnOp>();
}

mlir::Type CodeGen_MLIR::mlir_type_of(mlir::ImplicitLocOpBuilder &builder, Halide::Type t) {
    if (t.lanes() == 1) {
        if (t.is_int_or_uint()) {
            return builder.getIntegerType(t.bits());
        } else if (t.is_bfloat()) {
            return builder.getBF16Type();
        } else if (t.is_float()) {
            switch (t.bits()) {
            case 16:
                return builder.getF16Type();
            case 32:
                return builder.getF32Type();
            case 64:
                return builder.getF64Type();
            default:
                internal_error << "There is no MLIR type matching this floating-point bit width: " << t;
                return nullptr;
            }
        } else {
            internal_error << "Type not supported: " << t;
        }
    } else {
        return mlir::VectorType::get(t.lanes(), mlir_type_of(builder, t.element_of()));
    }

    return mlir::Type();
}

CodeGen_MLIR::Visitor::Visitor(mlir::ImplicitLocOpBuilder &builder, const LoweredFunc &func)
    : builder(builder) {

    mlir::func::FuncOp funcOp = cast<mlir::func::FuncOp>(builder.getBlock()->getParentOp());
    for (auto [index, arg] : llvm::enumerate(func.args)) {
        if (arg.is_buffer())
            sym_push(arg.name + ".buffer", funcOp.getArgument(index));
        else
            sym_push(arg.name, funcOp.getArgument(index));
    }
}

mlir::Value CodeGen_MLIR::Visitor::codegen(const Expr &e) {
    internal_assert(e.defined());
    debug(4) << "Codegen (E): " << e.type() << ", " << e;
    value = mlir::Value();
    e.accept(this);
    internal_assert(value) << "Codegen of an expr did not produce a MLIR value: " << e;
    return value;
}

void CodeGen_MLIR::Visitor::codegen(const Stmt &s) {
    internal_assert(s.defined());
    debug(4) << "Codegen (S): " << s;
    value = mlir::Value();
    s.accept(this);
}

void CodeGen_MLIR::Visitor::visit(const IntImm *op) {
    mlir::Type type = mlir_type_of(op->type);
    value = builder.create<mlir::arith::ConstantOp>(type, builder.getIntegerAttr(type, op->value));
}

void CodeGen_MLIR::Visitor::visit(const UIntImm *op) {
    mlir::Type type = mlir_type_of(op->type);
    value = builder.create<mlir::arith::ConstantOp>(type, builder.getIntegerAttr(type, op->value));
}

void CodeGen_MLIR::Visitor::visit(const FloatImm *op) {
    mlir::Type type = mlir_type_of(op->type);
    value = builder.create<mlir::arith::ConstantOp>(type, builder.getFloatAttr(type, op->value));
}

void CodeGen_MLIR::Visitor::visit(const StringImm *op) {
    internal_error << "String immediates are not supported";
}

void CodeGen_MLIR::Visitor::visit(const Cast *op) {
    Halide::Type src = op->value.type();
    Halide::Type dst = op->type;
    mlir::Type mlir_type = mlir_type_of(dst);

    value = codegen(op->value);

    if (src.is_int_or_uint() && dst.is_int_or_uint()) {
        if (dst.bits() > src.bits()) {
            if (src.is_int())
                value = builder.create<mlir::arith::ExtSIOp>(mlir_type, value);
            else
                value = builder.create<mlir::arith::ExtUIOp>(mlir_type, value);
        } else {
            value = builder.create<mlir::arith::TruncIOp>(mlir_type, value);
        }
    } else if (src.is_float() && dst.is_int()) {
        value = builder.create<mlir::arith::FPToSIOp>(mlir_type, value);
    } else if (src.is_float() && dst.is_uint()) {
        value = builder.create<mlir::arith::FPToUIOp>(mlir_type, value);
    } else if (src.is_int() && dst.is_float()) {
        value = builder.create<mlir::arith::SIToFPOp>(mlir_type, value);
    } else if (src.is_uint() && dst.is_float()) {
        value = builder.create<mlir::arith::UIToFPOp>(mlir_type, value);
    } else if (src.is_float() && dst.is_float()) {
        if (dst.bits() > src.bits()) {
            value = builder.create<mlir::arith::ExtFOp>(mlir_type, value);
        } else {
            value = builder.create<mlir::arith::TruncFOp>(mlir_type, value);
        }
    } else {
        internal_error << "Cast of " << src << " to " << dst << " is not implemented";
    }
}

void CodeGen_MLIR::Visitor::visit(const Reinterpret *op) {
    value = builder.create<mlir::arith::BitcastOp>(mlir_type_of(op->type), codegen(op->value));
}

void CodeGen_MLIR::Visitor::visit(const Variable *op) {
    value = sym_get(op->name, true);
}

template<typename IntOp, typename UIntOp, typename FloatOp>
mlir::Value CodeGen_MLIR::Visitor::BinaryOpHelper(const Halide::Type &type, const Halide::Expr &a, const Halide::Expr &b) {
    mlir::Value a_val = codegen(a);
    mlir::Value b_val = codegen(b);

    if (type.is_int()) {
        return builder.create<IntOp>(a_val, b_val);
    } else if (type.is_uint()) {
        return builder.create<UIntOp>(a_val, b_val);
    } else if (type.is_float()) {
        return builder.create<FloatOp>(a_val, b_val);
    } else {
        internal_error << "Unsupported type: " << type;
        return mlir::Value();
    }
}

template<typename IntOp, mlir::arith::CmpIPredicate IntPredicate,
         typename UIntOp, mlir::arith::CmpIPredicate UIntPredicate,
         typename FloatOp, mlir::arith::CmpFPredicate FloatPredicate>
mlir::Value CodeGen_MLIR::Visitor::CompareOpHelper(const Halide::Type &type, const Halide::Expr &a, const Halide::Expr &b) {
    mlir::Value a_val = codegen(a);
    mlir::Value b_val = codegen(b);

    if (type.is_int()) {
        return builder.create<IntOp>(IntPredicate, a_val, b_val);
    } else if (type.is_uint()) {
        return builder.create<UIntOp>(UIntPredicate, a_val, b_val);
    } else if (type.is_float()) {
        return builder.create<FloatOp>(FloatPredicate, a_val, b_val);
    } else {
        internal_error << "Unsupported type: " << type;
        return mlir::Value();
    }
}

void CodeGen_MLIR::Visitor::visit(const Add *op) {
    value = BinaryOpHelper<mlir::arith::AddIOp, mlir::arith::AddFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Sub *op) {
    value = BinaryOpHelper<mlir::arith::SubIOp, mlir::arith::SubFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Mul *op) {
    value = BinaryOpHelper<mlir::arith::MulIOp, mlir::arith::MulFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Div *op) {
    value = BinaryOpHelper<mlir::arith::DivSIOp, mlir::arith::DivUIOp, mlir::arith::DivFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Mod *op) {
    value = BinaryOpHelper<mlir::arith::RemSIOp, mlir::arith::RemUIOp, mlir::arith::RemFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Min *op) {
    value = BinaryOpHelper<mlir::arith::MinSIOp, mlir::arith::MinUIOp, mlir::arith::MinimumFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const Max *op) {
    value = BinaryOpHelper<mlir::arith::MaxSIOp, mlir::arith::MaxUIOp, mlir::arith::MaximumFOp>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const EQ *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::eq, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::OEQ>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const NE *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::ne, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::ONE>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const LT *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::slt, mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::ult, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::OLT>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const LE *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::sle, mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::ule, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::OLE>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const GT *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::sgt, mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::ugt, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::OGT>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const GE *op) {
    value = CompareOpHelper<mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::sge, mlir::arith::CmpIOp, mlir::arith::CmpIPredicate::uge, mlir::arith::CmpFOp, mlir::arith::CmpFPredicate::OGE>(op->type, op->a, op->b);
}

void CodeGen_MLIR::Visitor::visit(const And *op) {
    value = builder.create<mlir::arith::AndIOp>(codegen(op->a), codegen(op->b));
}

void CodeGen_MLIR::Visitor::visit(const Or *op) {
    value = builder.create<mlir::arith::OrIOp>(codegen(op->a), codegen(op->b));
}

void CodeGen_MLIR::Visitor::visit(const Not *op) {
    value = builder.create<mlir::arith::XOrIOp>(
        codegen(op->a), builder.create<mlir::arith::ConstantOp>(builder.getIntegerAttr(builder.getI1Type(), 1)));
}

void CodeGen_MLIR::Visitor::visit(const Select *op) {
    value = builder.create<mlir::arith::SelectOp>(codegen(op->condition),
                                                  codegen(op->true_value),
                                                  codegen(op->false_value));
}

void CodeGen_MLIR::Visitor::visit(const Load *op) {
    mlir::Value buffer = sym_get(op->name);
    mlir::Type type = mlir_type_of(op->type);
    mlir::Value index;
    if (op->type.is_scalar()) {
        index = codegen(op->index);
    } else if (Expr ramp_base = strided_ramp_base(op->index); ramp_base.defined()) {
        index = codegen(ramp_base);
    } else {
        internal_error << "Unsupported Load: " << Expr(op);
    }

    index = builder.create<mlir::arith::IndexCastOp>(builder.getIndexType(), index);
    if (op->type.is_scalar()) {
        value = builder.create<mlir::memref::LoadOp>(type, buffer, mlir::ValueRange{index});
    } else {
        value = builder.create<mlir::vector::LoadOp>(type, buffer, mlir::ValueRange{index});
    }
}

void CodeGen_MLIR::Visitor::visit(const Ramp *op) {
    mlir::Value base = codegen(op->base);
    mlir::Value stride = codegen(op->stride);
    mlir::Type elementType = mlir_type_of(op->base.type());
    mlir::VectorType vectorType = mlir::VectorType::get(op->lanes, elementType);

    mlir::SmallVector<mlir::Attribute> indicesAttrs(op->lanes);
    for (int i = 0; i < op->lanes; i++)
        indicesAttrs[i] = mlir::IntegerAttr::get(elementType, i);

    mlir::DenseElementsAttr indicesDenseAttr = mlir::DenseElementsAttr::get(vectorType, indicesAttrs);
    mlir::Value indicesConst = builder.create<mlir::arith::ConstantOp>(indicesDenseAttr);
    mlir::Value splatStride = builder.create<mlir::vector::SplatOp>(vectorType, stride);
    mlir::Value offsets = builder.create<mlir::arith::MulIOp>(splatStride, indicesConst);
    mlir::Value splatBase = builder.create<mlir::vector::SplatOp>(vectorType, base);
    value = builder.create<mlir::arith::AddIOp>(splatBase, offsets);
}

void CodeGen_MLIR::Visitor::visit(const Broadcast *op) {
    value = builder.create<mlir::vector::SplatOp>(mlir_type_of(op->type), codegen(op->value));
}

void CodeGen_MLIR::Visitor::visit(const Call *op) {
    if (op->is_intrinsic(Call::bitwise_and)) {
        value = builder.create<mlir::arith::AndIOp>(codegen(op->args[0]), codegen(op->args[1]));
    } else if (op->is_intrinsic(Call::shift_left)) {
        value = builder.create<mlir::arith::ShLIOp>(codegen(op->args[0]), codegen(op->args[1]));
    } else if (op->is_intrinsic(Call::shift_right)) {
        if (op->type.is_int())
            value = builder.create<mlir::arith::ShRSIOp>(codegen(op->args[0]), codegen(op->args[1]));
        else
            value = builder.create<mlir::arith::ShRUIOp>(codegen(op->args[0]), codegen(op->args[1]));
    } else if (op->is_intrinsic(Call::widen_right_mul)) {
        mlir::Value a = codegen(op->args[0]);
        mlir::Value b = codegen(op->args[1]);
        mlir::Type widen_type = mlir_type_of(op->type);
        if (op->type.is_int())
            b = builder.create<mlir::arith::ExtSIOp>(widen_type, b);
        else
            b = builder.create<mlir::arith::ExtUIOp>(widen_type, b);
        value = builder.create<mlir::arith::MulIOp>(a, b);
    } else if (op->name == Call::buffer_get_host) {
        value = codegen(op->args[0]);
    } else if (op->name == Call::buffer_get_min) {
        mlir::Type type = mlir_type_of(op->type);
        value = builder.create<mlir::arith::ConstantOp>(type, builder.getIntegerAttr(type, 0));
    } else if (op->name == Call::buffer_get_extent) {
        mlir::Type type = mlir_type_of(op->type);
        mlir::Value buffer = codegen(op->args[0]);
        mlir::Value index = codegen(op->args[1]);
        index = builder.create<mlir::arith::IndexCastOp>(builder.getIndexType(), index);
        mlir::Value dim = builder.create<mlir::memref::DimOp>(buffer, index);
        value = builder.create<mlir::arith::IndexCastOp>(type, dim);
    } else {
        internal_error << "Call to " << op->name << " not implemented";
    }
}

void CodeGen_MLIR::Visitor::visit(const Let *op) {
    sym_push(op->name, codegen(op->value));
    value = codegen(op->body);
    sym_pop(op->name);
}

void CodeGen_MLIR::Visitor::visit(const LetStmt *op) {
    sym_push(op->name, codegen(op->value));
    codegen(op->body);
    sym_pop(op->name);
}

void CodeGen_MLIR::Visitor::visit(const AssertStmt *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const ProducerConsumer *op) {
    codegen(op->body);
}

void CodeGen_MLIR::Visitor::visit(const For *op) {
    mlir::Value min = codegen(op->min);
    mlir::Value max = builder.create<mlir::arith::AddIOp>(min, codegen(op->extent));
    mlir::Value lb = builder.create<mlir::arith::IndexCastOp>(builder.getIndexType(), min);
    mlir::Value ub = builder.create<mlir::arith::IndexCastOp>(builder.getIndexType(), max);
    mlir::Value step = builder.create<mlir::arith::ConstantIndexOp>(1);

    mlir::scf::ForOp forOp = builder.create<mlir::scf::ForOp>(lb, ub, step);
    {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(forOp.getBody());

        mlir::Value i = forOp.getInductionVar();
        sym_push(op->name, builder.create<mlir::arith::IndexCastOp>(max.getType(), i));
        codegen(op->body);
        sym_pop(op->name);
    }
}

void CodeGen_MLIR::Visitor::visit(const Store *op) {
    mlir::Value buffer = sym_get(op->name);
    mlir::Value value = codegen(op->value);
    mlir::Value index;
    if (op->value.type().is_scalar()) {
        index = codegen(op->index);
    } else if (Expr ramp_base = strided_ramp_base(op->index); ramp_base.defined()) {
        index = codegen(ramp_base);
    } else {
        internal_error << "Unsupported Store: " << Stmt(op);
    }

    index = builder.create<mlir::arith::IndexCastOp>(builder.getIndexType(), index);
    if (op->value.type().is_scalar())
        builder.create<mlir::memref::StoreOp>(value, buffer, mlir::ValueRange{index});
    else
        builder.create<mlir::vector::StoreOp>(value, buffer, mlir::ValueRange{index});
}

void CodeGen_MLIR::Visitor::visit(const Provide *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const Allocate *op) {
    int32_t size = op->constant_allocation_size();
    internal_assert(size != 0) << "Allocation must have constant size for MLIR codegen";
    mlir::MemRefType type = mlir::MemRefType::get({size}, mlir_type_of(op->type));
    mlir::memref::AllocOp alloc = builder.create<mlir::memref::AllocOp>(type);

    sym_push(op->name, alloc);
    codegen(op->body);
    sym_pop(op->name);
}

void CodeGen_MLIR::Visitor::visit(const Free *op) {
    builder.create<mlir::memref::DeallocOp>(sym_get(op->name));
}

void CodeGen_MLIR::Visitor::visit(const Realize *op) {
    internal_error << "Realize in CodeGen";
}

void CodeGen_MLIR::Visitor::visit(const Block *op) {
    // Peel blocks of assertions with pure conditions
    const AssertStmt *a = op->first.as<AssertStmt>();
    if (a && is_pure(a->condition)) {
        std::vector<const AssertStmt *> asserts;
        asserts.push_back(a);
        Stmt s = op->rest;
        while ((op = s.as<Block>()) && (a = op->first.as<AssertStmt>()) && is_pure(a->condition) && asserts.size() < 63) {
            asserts.push_back(a);
            s = op->rest;
        }
        // TODO
        // codegen_asserts(asserts);
        codegen(s);
    } else {
        codegen(op->first);
        codegen(op->rest);
    }
}

void CodeGen_MLIR::Visitor::visit(const IfThenElse *op) {
    mlir::scf::IfOp ifOp = builder.create<mlir::scf::IfOp>(codegen(op->condition), op->else_case.defined());
    {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(ifOp.thenBlock());
        codegen(op->then_case);
    }

    if (op->else_case.defined()) {
        mlir::OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(ifOp.elseBlock());
        codegen(op->else_case);
    }
}

void CodeGen_MLIR::Visitor::visit(const Evaluate *op) {
    codegen(op->value);
    // Discard result
    value = mlir::Value();
}

void CodeGen_MLIR::Visitor::visit(const Shuffle *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const VectorReduce *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const Prefetch *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const Fork *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const Acquire *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const Atomic *op) {
    internal_error << "Unimplemented";
}

void CodeGen_MLIR::Visitor::visit(const HoistedStorage *op) {
    internal_error << "Unimplemented";
}

mlir::Type CodeGen_MLIR::Visitor::mlir_type_of(Halide::Type t) const {
    return CodeGen_MLIR::mlir_type_of(builder, t);
}

void CodeGen_MLIR::Visitor::sym_push(const std::string &name, mlir::Value value) {
    symbol_table.push(name, value);
}

void CodeGen_MLIR::Visitor::sym_pop(const std::string &name) {
    symbol_table.pop(name);
}

mlir::Value CodeGen_MLIR::Visitor::sym_get(const std::string &name, bool must_succeed) const {
    // look in the symbol table
    if (const auto *v = symbol_table.find(name)) {
        return *v;
    }
    if (must_succeed) {
        debug(1) << "The following names are in scope:\n"
                 << symbol_table;
        internal_error << "Symbol not found: " << name;
    }
    return nullptr;
}

}  // namespace Internal
}  // namespace Halide

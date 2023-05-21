#ifndef HALIDE_CODEGEN_MLIR_H
#define HALIDE_CODEGEN_MLIR_H

/** \file
 * Defines the code-generator for producing MLIR code
 */

#include "IRVisitor.h"
#include "Scope.h"

#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/ImplicitLocOpBuilder.h>

namespace Halide {

struct Target;

namespace Internal {

struct LoweredFunc;

class CodeGen_MLIR {
public:
    CodeGen_MLIR(std::ostream &stream);

    void compile(const Module &module);

protected:
    void compile_func(mlir::ImplicitLocOpBuilder &builder, const LoweredFunc &func);

    static mlir::Type mlir_type_of(mlir::ImplicitLocOpBuilder &builder, Halide::Type t);

    class Visitor : public IRVisitor {
    public:
        Visitor(mlir::ImplicitLocOpBuilder &builder, const LoweredFunc &func);

    protected:
        mlir::Value codegen(const Expr &);
        void codegen(const Stmt &);

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
        void visit(const ProducerConsumer *) override;
        void visit(const For *) override;
        void visit(const Store *) override;
        void visit(const Provide *) override;
        void visit(const Allocate *) override;
        void visit(const Free *) override;
        void visit(const Realize *) override;
        void visit(const Block *) override;
        void visit(const IfThenElse *) override;
        void visit(const Evaluate *) override;
        void visit(const Shuffle *) override;
        void visit(const VectorReduce *) override;
        void visit(const Prefetch *) override;
        void visit(const Fork *) override;
        void visit(const Acquire *) override;
        void visit(const Atomic *) override;
        void visit(const HoistedStorage *) override;

        mlir::Type mlir_type_of(Halide::Type t) const;

        void sym_push(const std::string &name, mlir::Value value);
        void sym_pop(const std::string &name);
        mlir::Value sym_get(const std::string &name, bool must_succeed = true) const;

    private:
        mlir::ImplicitLocOpBuilder &builder;
        mlir::Value value;
        Scope<mlir::Value> symbol_table;
    };

    mlir::MLIRContext mlir_context;
    std::ostream &stream;
};

}  // namespace Internal
}  // namespace Halide

#endif

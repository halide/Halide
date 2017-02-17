#ifndef HALIDE_IR_MUTATOR_H
#define HALIDE_IR_MUTATOR_H

/** \file
 * Defines a base class for passes over the IR that modify it
 */

#include "IRVisitor.h"

namespace Halide {
namespace Internal {

/** A base class for passes over the IR which modify it
 * (e.g. replacing a variable with a value (Substitute.h), or
 * constant-folding).
 *
 * Your mutate should override the visit methods you care about. Return
 * the new expression by assigning to expr or stmt. The default ones
 * recursively mutate their children. To mutate sub-expressions and
 * sub-statements you should the mutate method, which will dispatch to
 * the appropriate visit method and then return the value of expr or
 * stmt after the call to visit.
 */
class IRMutator : public IRVisitor {
public:

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    EXPORT virtual Expr mutate(Expr expr);
    EXPORT virtual Stmt mutate(Stmt stmt);

protected:

    /** visit methods that take Exprs assign to this to return their
     * new value */
    Expr expr;

    /** visit methods that take Stmts assign to this to return their
     * new value */
    Stmt stmt;

    EXPORT void visit(const IntImm *) override;
    EXPORT void visit(const UIntImm *) override;
    EXPORT void visit(const FloatImm *) override;
    EXPORT void visit(const StringImm *) override;
    EXPORT void visit(const Cast *) override;
    EXPORT void visit(const Variable *) override;
    EXPORT void visit(const Add *) override;
    EXPORT void visit(const Sub *) override;
    EXPORT void visit(const Mul *) override;
    EXPORT void visit(const Div *) override;
    EXPORT void visit(const Mod *) override;
    EXPORT void visit(const Min *) override;
    EXPORT void visit(const Max *) override;
    EXPORT void visit(const EQ *) override;
    EXPORT void visit(const NE *) override;
    EXPORT void visit(const LT *) override;
    EXPORT void visit(const LE *) override;
    EXPORT void visit(const GT *) override;
    EXPORT void visit(const GE *) override;
    EXPORT void visit(const And *) override;
    EXPORT void visit(const Or *) override;
    EXPORT void visit(const Not *) override;
    EXPORT void visit(const Select *) override;
    EXPORT void visit(const Load *) override;
    EXPORT void visit(const Ramp *) override;
    EXPORT void visit(const Broadcast *) override;
    EXPORT void visit(const Call *) override;
    EXPORT void visit(const Let *) override;
    EXPORT void visit(const LetStmt *) override;
    EXPORT void visit(const AssertStmt *) override;
    EXPORT void visit(const ProducerConsumer *) override;
    EXPORT void visit(const For *) override;
    EXPORT void visit(const Store *) override;
    EXPORT void visit(const Provide *) override;
    EXPORT void visit(const Allocate *) override;
    EXPORT void visit(const Free *) override;
    EXPORT void visit(const Realize *) override;
    EXPORT void visit(const Block *) override;
    EXPORT void visit(const IfThenElse *) override;
    EXPORT void visit(const Evaluate *) override;
    EXPORT void visit(const Shuffle *) override;
};


/** A mutator that caches and reapplies previously-done mutations, so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator : public IRMutator {
protected:
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    EXPORT Stmt mutate(Stmt s) override;
    EXPORT Expr mutate(Expr e) override;
};


}
}

#endif

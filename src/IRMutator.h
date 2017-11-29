#ifndef HALIDE_IR_MUTATOR_H
#define HALIDE_IR_MUTATOR_H

/** \file
 * Defines a base class for passes over the IR that modify it
 */

#include "IRVisitor.h"

namespace Halide {
namespace Internal {

/**
 * Deprecated for new use: please use IRMutator2 instead.
 * Existing usage of IRMutator will be migrated to IRMutator2 and
 * this class will be removed.
 *
 * A base class for passes over the IR which modify it
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
    EXPORT IRMutator();
    EXPORT virtual ~IRMutator();

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    EXPORT virtual Expr mutate(const Expr &expr);
    EXPORT virtual Stmt mutate(const Stmt &stmt);

protected:

    /** visit methods that take Exprs assign to this to return their
     * new value */
    Expr expr;

    /** visit methods that take Stmts assign to this to return their
     * new value */
    Stmt stmt;

    EXPORT virtual void visit(const IntImm *);
    EXPORT virtual void visit(const UIntImm *);
    EXPORT virtual void visit(const FloatImm *);
    EXPORT virtual void visit(const StringImm *);
    EXPORT virtual void visit(const Cast *);
    EXPORT virtual void visit(const Variable *);
    EXPORT virtual void visit(const Add *);
    EXPORT virtual void visit(const Sub *);
    EXPORT virtual void visit(const Mul *);
    EXPORT virtual void visit(const Div *);
    EXPORT virtual void visit(const Mod *);
    EXPORT virtual void visit(const Min *);
    EXPORT virtual void visit(const Max *);
    EXPORT virtual void visit(const EQ *);
    EXPORT virtual void visit(const NE *);
    EXPORT virtual void visit(const LT *);
    EXPORT virtual void visit(const LE *);
    EXPORT virtual void visit(const GT *);
    EXPORT virtual void visit(const GE *);
    EXPORT virtual void visit(const And *);
    EXPORT virtual void visit(const Or *);
    EXPORT virtual void visit(const Not *);
    EXPORT virtual void visit(const Select *);
    EXPORT virtual void visit(const Load *);
    EXPORT virtual void visit(const Ramp *);
    EXPORT virtual void visit(const Broadcast *);
    EXPORT virtual void visit(const Call *);
    EXPORT virtual void visit(const Let *);
    EXPORT virtual void visit(const LetStmt *);
    EXPORT virtual void visit(const AssertStmt *);
    EXPORT virtual void visit(const ProducerConsumer *);
    EXPORT virtual void visit(const For *);
    EXPORT virtual void visit(const Store *);
    EXPORT virtual void visit(const Provide *);
    EXPORT virtual void visit(const Allocate *);
    EXPORT virtual void visit(const Free *);
    EXPORT virtual void visit(const Realize *);
    EXPORT virtual void visit(const Block *);
    EXPORT virtual void visit(const IfThenElse *);
    EXPORT virtual void visit(const Evaluate *);
    EXPORT virtual void visit(const Shuffle *);
    EXPORT virtual void visit(const Prefetch *);
};


/** A base class for passes over the IR which modify it
 * (e.g. replacing a variable with a value (Substitute.h), or
 * constant-folding).
 *
 * Your mutator should override the visit() methods you care about and return
 * the new expression or stmt. The default implementations recursively
 * mutate their children. To mutate sub-expressions and sub-statements you
 * should override the mutate() method, which will dispatch to
 * the appropriate visit() method and then return the value of expr or
 * stmt after the call to visit.
 */
class IRMutator2 {
public:
    EXPORT IRMutator2();
    EXPORT virtual ~IRMutator2();

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    EXPORT virtual Expr mutate(const Expr &expr);
    EXPORT virtual Stmt mutate(const Stmt &stmt);

protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement mutate_expr/mutate_stmt())
    template<typename T> friend struct ExprNode;
    template<typename T> friend struct StmtNode;

    EXPORT virtual Expr visit(const IntImm *);
    EXPORT virtual Expr visit(const UIntImm *);
    EXPORT virtual Expr visit(const FloatImm *);
    EXPORT virtual Expr visit(const StringImm *);
    EXPORT virtual Expr visit(const Cast *);
    EXPORT virtual Expr visit(const Variable *);
    EXPORT virtual Expr visit(const Add *);
    EXPORT virtual Expr visit(const Sub *);
    EXPORT virtual Expr visit(const Mul *);
    EXPORT virtual Expr visit(const Div *);
    EXPORT virtual Expr visit(const Mod *);
    EXPORT virtual Expr visit(const Min *);
    EXPORT virtual Expr visit(const Max *);
    EXPORT virtual Expr visit(const EQ *);
    EXPORT virtual Expr visit(const NE *);
    EXPORT virtual Expr visit(const LT *);
    EXPORT virtual Expr visit(const LE *);
    EXPORT virtual Expr visit(const GT *);
    EXPORT virtual Expr visit(const GE *);
    EXPORT virtual Expr visit(const And *);
    EXPORT virtual Expr visit(const Or *);
    EXPORT virtual Expr visit(const Not *);
    EXPORT virtual Expr visit(const Select *);
    EXPORT virtual Expr visit(const Load *);
    EXPORT virtual Expr visit(const Ramp *);
    EXPORT virtual Expr visit(const Broadcast *);
    EXPORT virtual Expr visit(const Call *);
    EXPORT virtual Expr visit(const Let *);
    EXPORT virtual Expr visit(const Shuffle *);

    EXPORT virtual Stmt visit(const LetStmt *);
    EXPORT virtual Stmt visit(const AssertStmt *);
    EXPORT virtual Stmt visit(const ProducerConsumer *);
    EXPORT virtual Stmt visit(const For *);
    EXPORT virtual Stmt visit(const Store *);
    EXPORT virtual Stmt visit(const Provide *);
    EXPORT virtual Stmt visit(const Allocate *);
    EXPORT virtual Stmt visit(const Free *);
    EXPORT virtual Stmt visit(const Realize *);
    EXPORT virtual Stmt visit(const Block *);
    EXPORT virtual Stmt visit(const IfThenElse *);
    EXPORT virtual Stmt visit(const Evaluate *);
    EXPORT virtual Stmt visit(const Prefetch *);
};

/** A mutator that caches and reapplies previously-done mutations, so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator2 : public IRMutator2 {
protected:
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    EXPORT Stmt mutate(const Stmt &s) override;
    EXPORT Expr mutate(const Expr &e) override;
};

}
}

#endif

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
    IRMutator();
    virtual ~IRMutator();

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    virtual Expr mutate(const Expr &expr);
    virtual Stmt mutate(const Stmt &stmt);

protected:

    /** visit methods that take Exprs assign to this to return their
     * new value */
    Expr expr;

    /** visit methods that take Stmts assign to this to return their
     * new value */
    Stmt stmt;

    virtual void visit(const IntImm *);
    virtual void visit(const UIntImm *);
    virtual void visit(const FloatImm *);
    virtual void visit(const StringImm *);
    virtual void visit(const Cast *);
    virtual void visit(const Variable *);
    virtual void visit(const Add *);
    virtual void visit(const Sub *);
    virtual void visit(const Mul *);
    virtual void visit(const Div *);
    virtual void visit(const Mod *);
    virtual void visit(const Min *);
    virtual void visit(const Max *);
    virtual void visit(const EQ *);
    virtual void visit(const NE *);
    virtual void visit(const LT *);
    virtual void visit(const LE *);
    virtual void visit(const GT *);
    virtual void visit(const GE *);
    virtual void visit(const And *);
    virtual void visit(const Or *);
    virtual void visit(const Not *);
    virtual void visit(const Select *);
    virtual void visit(const Load *);
    virtual void visit(const Ramp *);
    virtual void visit(const Broadcast *);
    virtual void visit(const Call *);
    virtual void visit(const Let *);
    virtual void visit(const LetStmt *);
    virtual void visit(const AssertStmt *);
    virtual void visit(const ProducerConsumer *);
    virtual void visit(const For *);
    virtual void visit(const Acquire *);
    virtual void visit(const Store *);
    virtual void visit(const Provide *);
    virtual void visit(const Allocate *);
    virtual void visit(const Free *);
    virtual void visit(const Realize *);
    virtual void visit(const Block *);
    virtual void visit(const Fork *);
    virtual void visit(const IfThenElse *);
    virtual void visit(const Evaluate *);
    virtual void visit(const Shuffle *);
    virtual void visit(const Prefetch *);
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
    IRMutator2();
    virtual ~IRMutator2();

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    virtual Expr mutate(const Expr &expr);
    virtual Stmt mutate(const Stmt &stmt);

protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement mutate_expr/mutate_stmt())
    template<typename T> friend struct ExprNode;
    template<typename T> friend struct StmtNode;

    virtual Expr visit(const IntImm *);
    virtual Expr visit(const UIntImm *);
    virtual Expr visit(const FloatImm *);
    virtual Expr visit(const StringImm *);
    virtual Expr visit(const Cast *);
    virtual Expr visit(const Variable *);
    virtual Expr visit(const Add *);
    virtual Expr visit(const Sub *);
    virtual Expr visit(const Mul *);
    virtual Expr visit(const Div *);
    virtual Expr visit(const Mod *);
    virtual Expr visit(const Min *);
    virtual Expr visit(const Max *);
    virtual Expr visit(const EQ *);
    virtual Expr visit(const NE *);
    virtual Expr visit(const LT *);
    virtual Expr visit(const LE *);
    virtual Expr visit(const GT *);
    virtual Expr visit(const GE *);
    virtual Expr visit(const And *);
    virtual Expr visit(const Or *);
    virtual Expr visit(const Not *);
    virtual Expr visit(const Select *);
    virtual Expr visit(const Load *);
    virtual Expr visit(const Ramp *);
    virtual Expr visit(const Broadcast *);
    virtual Expr visit(const Call *);
    virtual Expr visit(const Let *);
    virtual Expr visit(const Shuffle *);

    virtual Stmt visit(const LetStmt *);
    virtual Stmt visit(const AssertStmt *);
    virtual Stmt visit(const ProducerConsumer *);
    virtual Stmt visit(const For *);
    virtual Stmt visit(const Store *);
    virtual Stmt visit(const Provide *);
    virtual Stmt visit(const Allocate *);
    virtual Stmt visit(const Free *);
    virtual Stmt visit(const Realize *);
    virtual Stmt visit(const Block *);
    virtual Stmt visit(const IfThenElse *);
    virtual Stmt visit(const Evaluate *);
    virtual Stmt visit(const Prefetch *);
    virtual Stmt visit(const Acquire *);
    virtual Stmt visit(const Fork *);
};

/** A mutator that caches and reapplies previously-done mutations, so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator2 : public IRMutator2 {
protected:
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    Stmt mutate(const Stmt &s) override;
    Expr mutate(const Expr &e) override;
};

/** A helper function for mutator-like things to mutate regions */
template<typename Mutator, typename... Args>
std::pair<Region, bool> mutate_region(Mutator *mutator, const Region &bounds, Args&&... args) {
    Region new_bounds(bounds.size());
    bool bounds_changed = false;

    for (size_t i = 0; i < bounds.size(); i++) {
        Expr old_min = bounds[i].min;
        Expr old_extent = bounds[i].extent;
        Expr new_min = mutator->mutate(old_min, std::forward<Args>(args)...);
        Expr new_extent = mutator->mutate(old_extent, std::forward<Args>(args)...);
        if (!new_min.same_as(old_min)) {
            bounds_changed = true;
        }
        if (!new_extent.same_as(old_extent)) {
            bounds_changed = true;
        }
        new_bounds[i] = Range(new_min, new_extent);
    }
    return {new_bounds, bounds_changed};
}

}  // namespace Internal
}  // namespace Halide

#endif

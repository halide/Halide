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


/**
 * Deprecated for new use: please use IRGraphMutator2 instead.
 * Existing usage of IRGraphMutator will be migrated to IRGraphMutator2 and
 * this class will be removed.
 *
 * A mutator that caches and reapplies previously-done mutations, so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator : public IRMutator {
protected:
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    EXPORT Stmt mutate(const Stmt &s);
    EXPORT Expr mutate(const Expr &e);
};

/** A base class for passes over the IR which modify it
 * (e.g. replacing a variable with a value (Substitute.h), or
 * constant-folding).
 *
 * Your mutator should override the mvisit() methods you care about and return
 * the new expression or stmt. The default implementations recursively
 * mutate their children. To mutate sub-expressions and sub-statements you
 * should override the mutate() method, which will dispatch to
 * the appropriate mvisit() method and then return the value of expr or
 * stmt after the call to visit.
 */
class IRMutator2 {
public:
    EXPORT virtual ~IRMutator2();

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    EXPORT virtual Expr mutate(const Expr &expr);
    EXPORT virtual Stmt mutate(const Stmt &stmt);

public:

    EXPORT virtual Expr mvisit(const IntImm *);
    EXPORT virtual Expr mvisit(const UIntImm *);
    EXPORT virtual Expr mvisit(const FloatImm *);
    EXPORT virtual Expr mvisit(const StringImm *);
    EXPORT virtual Expr mvisit(const Cast *);
    EXPORT virtual Expr mvisit(const Variable *);
    EXPORT virtual Expr mvisit(const Add *);
    EXPORT virtual Expr mvisit(const Sub *);
    EXPORT virtual Expr mvisit(const Mul *);
    EXPORT virtual Expr mvisit(const Div *);
    EXPORT virtual Expr mvisit(const Mod *);
    EXPORT virtual Expr mvisit(const Min *);
    EXPORT virtual Expr mvisit(const Max *);
    EXPORT virtual Expr mvisit(const EQ *);
    EXPORT virtual Expr mvisit(const NE *);
    EXPORT virtual Expr mvisit(const LT *);
    EXPORT virtual Expr mvisit(const LE *);
    EXPORT virtual Expr mvisit(const GT *);
    EXPORT virtual Expr mvisit(const GE *);
    EXPORT virtual Expr mvisit(const And *);
    EXPORT virtual Expr mvisit(const Or *);
    EXPORT virtual Expr mvisit(const Not *);
    EXPORT virtual Expr mvisit(const Select *);
    EXPORT virtual Expr mvisit(const Load *);
    EXPORT virtual Expr mvisit(const Ramp *);
    EXPORT virtual Expr mvisit(const Broadcast *);
    EXPORT virtual Expr mvisit(const Call *);
    EXPORT virtual Expr mvisit(const Let *);
    EXPORT virtual Expr mvisit(const Shuffle *);

    EXPORT virtual Stmt mvisit(const LetStmt *);
    EXPORT virtual Stmt mvisit(const AssertStmt *);
    EXPORT virtual Stmt mvisit(const ProducerConsumer *);
    EXPORT virtual Stmt mvisit(const For *);
    EXPORT virtual Stmt mvisit(const Store *);
    EXPORT virtual Stmt mvisit(const Provide *);
    EXPORT virtual Stmt mvisit(const Allocate *);
    EXPORT virtual Stmt mvisit(const Free *);
    EXPORT virtual Stmt mvisit(const Realize *);
    EXPORT virtual Stmt mvisit(const Block *);
    EXPORT virtual Stmt mvisit(const IfThenElse *);
    EXPORT virtual Stmt mvisit(const Evaluate *);
    EXPORT virtual Stmt mvisit(const Prefetch *);
};

/** A mutator that caches and reapplies previously-done mutations, so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator2 : public IRMutator2 {
protected:
    // TODO: would std::unordered_map be a performance win here?
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    EXPORT Stmt mutate(const Stmt &s) override;
    EXPORT Expr mutate(const Expr &e) override;
};

}
}

#endif

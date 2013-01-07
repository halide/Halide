#ifndef HALIDE_IR_MUTATOR_H
#define HALIDE_IR_MUTATOR_H

#include "IRVisitor.h"
#include "IR.h"

#include <vector>
#include <utility>

namespace Halide { 
namespace Internal {

/* Here is a base class for passes over the IR which change bits
 * of IR (e.g. replacing a variable with a value (Substitute.h), or
 * constant-folding).
 */
class IRMutator : public IRVisitor {
public:

    /* This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    Expr mutate(Expr expr);
    Stmt mutate(Stmt stmt);

protected:


    Expr expr;
    Stmt stmt;

    /* Override some of the visit functions below to achieve your
     * goals. Their default implementations just recursively
     * mutate their children. Put the result in either expr or stmt.
     */

    virtual void visit(const IntImm *);
    virtual void visit(const FloatImm *);
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
    virtual void visit(const PrintStmt *);
    virtual void visit(const AssertStmt *);
    virtual void visit(const Pipeline *);
    virtual void visit(const For *);
    virtual void visit(const Store *);
    virtual void visit(const Provide *);
    virtual void visit(const Allocate *);
    virtual void visit(const Realize *);
    virtual void visit(const Block *);

};    

}
}

#endif

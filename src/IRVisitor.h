#ifndef HALIDE_IR_VISITOR_H
#define HALIDE_IR_VISITOR_H

#include "IR.h"
#include "Util.h"

#include <set>
#include <map>
#include <string>

/** \file
 * Defines the base class for things that recursively walk over the IR
 */

namespace Halide {
namespace Internal {

/** A base class for algorithms that need to recursively walk over the
 * IR. The default implementations just recursively walk over the
 * children. Override the ones you care about.
 */
class IRVisitor {
public:
    EXPORT IRVisitor();
    EXPORT virtual ~IRVisitor();
protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement accept())
    template<typename T> friend struct ExprNode;
    template<typename T> friend struct StmtNode;

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

/** A base class for algorithms that walk recursively over the IR
 * without visiting the same node twice. This is for passes that are
 * capable of interpreting the IR as a DAG instead of a tree. */
class IRGraphVisitor : public IRVisitor {
protected:
    /** By default these methods add the node to the visited set, and
     * return whether or not it was already there. If it wasn't there,
     * it delegates to the appropriate visit method. You can override
     * them if you like. */
    // @{
    EXPORT virtual void include(const Expr &);
    EXPORT virtual void include(const Stmt &);
    // @}

private:
    /** The nodes visited so far */
    std::set<const IRNode *> visited;

protected:
    /** These methods should call 'include' on the children to only
     * visit them if they haven't been visited already. */
    // @{
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
    EXPORT void visit(const Prefetch *) override;
    // @}
};

}
}

#endif

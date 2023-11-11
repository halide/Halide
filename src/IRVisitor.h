#ifndef HALIDE_IR_VISITOR_H
#define HALIDE_IR_VISITOR_H

#include <set>

#include "IR.h"

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
    IRVisitor() = default;
    virtual ~IRVisitor() = default;

protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement accept())
    template<typename T>
    friend struct ExprNode;

    template<typename T>
    friend struct StmtNode;

    virtual void visit(const IntImm *);
    virtual void visit(const UIntImm *);
    virtual void visit(const FloatImm *);
    virtual void visit(const StringImm *);
    virtual void visit(const Cast *);
    virtual void visit(const Reinterpret *);
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
    virtual void visit(const Store *);
    virtual void visit(const Provide *);
    virtual void visit(const Allocate *);
    virtual void visit(const Free *);
    virtual void visit(const Realize *);
    virtual void visit(const Block *);
    virtual void visit(const IfThenElse *);
    virtual void visit(const Evaluate *);
    virtual void visit(const Shuffle *);
    virtual void visit(const VectorReduce *);
    virtual void visit(const Prefetch *);
    virtual void visit(const Fork *);
    virtual void visit(const Acquire *);
    virtual void visit(const Atomic *);
    virtual void visit(const HoistedStorage *);
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
    virtual void include(const Expr &);
    virtual void include(const Stmt &);
    // @}

private:
    /** The nodes visited so far */
    std::set<IRHandle> visited;

protected:
    /** These methods should call 'include' on the children to only
     * visit them if they haven't been visited already. */
    // @{
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
    void visit(const Acquire *) override;
    void visit(const Fork *) override;
    void visit(const Atomic *) override;
    void visit(const HoistedStorage *) override;
    // @}
};

/** A visitor/mutator capable of passing arbitrary arguments to the
 * visit methods using CRTP and returning any types from them. All
 * Expr visitors must have the same signature, and all Stmt visitors
 * must have the same signature. Does not have default implementations
 * of the visit methods. */
template<typename T, typename ExprRet, typename StmtRet>
class VariadicVisitor {
private:
    template<typename... Args>
    ExprRet dispatch_expr(const BaseExprNode *node, Args &&...args) {
        if (node == nullptr) {
            return ExprRet{};
        }
        switch (node->node_type) {
        case IRNodeType::IntImm:
            return ((T *)this)->visit((const IntImm *)node, std::forward<Args>(args)...);
        case IRNodeType::UIntImm:
            return ((T *)this)->visit((const UIntImm *)node, std::forward<Args>(args)...);
        case IRNodeType::FloatImm:
            return ((T *)this)->visit((const FloatImm *)node, std::forward<Args>(args)...);
        case IRNodeType::StringImm:
            return ((T *)this)->visit((const StringImm *)node, std::forward<Args>(args)...);
        case IRNodeType::Broadcast:
            return ((T *)this)->visit((const Broadcast *)node, std::forward<Args>(args)...);
        case IRNodeType::Cast:
            return ((T *)this)->visit((const Cast *)node, std::forward<Args>(args)...);
        case IRNodeType::Reinterpret:
            return ((T *)this)->visit((const Reinterpret *)node, std::forward<Args>(args)...);
        case IRNodeType::Variable:
            return ((T *)this)->visit((const Variable *)node, std::forward<Args>(args)...);
        case IRNodeType::Add:
            return ((T *)this)->visit((const Add *)node, std::forward<Args>(args)...);
        case IRNodeType::Sub:
            return ((T *)this)->visit((const Sub *)node, std::forward<Args>(args)...);
        case IRNodeType::Mod:
            return ((T *)this)->visit((const Mod *)node, std::forward<Args>(args)...);
        case IRNodeType::Mul:
            return ((T *)this)->visit((const Mul *)node, std::forward<Args>(args)...);
        case IRNodeType::Div:
            return ((T *)this)->visit((const Div *)node, std::forward<Args>(args)...);
        case IRNodeType::Min:
            return ((T *)this)->visit((const Min *)node, std::forward<Args>(args)...);
        case IRNodeType::Max:
            return ((T *)this)->visit((const Max *)node, std::forward<Args>(args)...);
        case IRNodeType::EQ:
            return ((T *)this)->visit((const EQ *)node, std::forward<Args>(args)...);
        case IRNodeType::NE:
            return ((T *)this)->visit((const NE *)node, std::forward<Args>(args)...);
        case IRNodeType::LT:
            return ((T *)this)->visit((const LT *)node, std::forward<Args>(args)...);
        case IRNodeType::LE:
            return ((T *)this)->visit((const LE *)node, std::forward<Args>(args)...);
        case IRNodeType::GT:
            return ((T *)this)->visit((const GT *)node, std::forward<Args>(args)...);
        case IRNodeType::GE:
            return ((T *)this)->visit((const GE *)node, std::forward<Args>(args)...);
        case IRNodeType::And:
            return ((T *)this)->visit((const And *)node, std::forward<Args>(args)...);
        case IRNodeType::Or:
            return ((T *)this)->visit((const Or *)node, std::forward<Args>(args)...);
        case IRNodeType::Not:
            return ((T *)this)->visit((const Not *)node, std::forward<Args>(args)...);
        case IRNodeType::Select:
            return ((T *)this)->visit((const Select *)node, std::forward<Args>(args)...);
        case IRNodeType::Load:
            return ((T *)this)->visit((const Load *)node, std::forward<Args>(args)...);
        case IRNodeType::Ramp:
            return ((T *)this)->visit((const Ramp *)node, std::forward<Args>(args)...);
        case IRNodeType::Call:
            return ((T *)this)->visit((const Call *)node, std::forward<Args>(args)...);
        case IRNodeType::Let:
            return ((T *)this)->visit((const Let *)node, std::forward<Args>(args)...);
        case IRNodeType::Shuffle:
            return ((T *)this)->visit((const Shuffle *)node, std::forward<Args>(args)...);
        case IRNodeType::VectorReduce:
            return ((T *)this)->visit((const VectorReduce *)node, std::forward<Args>(args)...);
            // Explicitly list the Stmt types rather than using a
            // default case so that when new IR nodes are added we
            // don't miss them here.
        case IRNodeType::LetStmt:
        case IRNodeType::AssertStmt:
        case IRNodeType::ProducerConsumer:
        case IRNodeType::For:
        case IRNodeType::Acquire:
        case IRNodeType::Store:
        case IRNodeType::Provide:
        case IRNodeType::Allocate:
        case IRNodeType::Free:
        case IRNodeType::Realize:
        case IRNodeType::Block:
        case IRNodeType::Fork:
        case IRNodeType::IfThenElse:
        case IRNodeType::Evaluate:
        case IRNodeType::Prefetch:
        case IRNodeType::Atomic:
        case IRNodeType::HoistedStorage:
            internal_error << "Unreachable";
        }
        return ExprRet{};
    }

    template<typename... Args>
    StmtRet dispatch_stmt(const BaseStmtNode *node, Args &&...args) {
        if (node == nullptr) {
            return StmtRet{};
        }
        switch (node->node_type) {
        case IRNodeType::IntImm:
        case IRNodeType::UIntImm:
        case IRNodeType::FloatImm:
        case IRNodeType::StringImm:
        case IRNodeType::Broadcast:
        case IRNodeType::Cast:
        case IRNodeType::Reinterpret:
        case IRNodeType::Variable:
        case IRNodeType::Add:
        case IRNodeType::Sub:
        case IRNodeType::Mod:
        case IRNodeType::Mul:
        case IRNodeType::Div:
        case IRNodeType::Min:
        case IRNodeType::Max:
        case IRNodeType::EQ:
        case IRNodeType::NE:
        case IRNodeType::LT:
        case IRNodeType::LE:
        case IRNodeType::GT:
        case IRNodeType::GE:
        case IRNodeType::And:
        case IRNodeType::Or:
        case IRNodeType::Not:
        case IRNodeType::Select:
        case IRNodeType::Load:
        case IRNodeType::Ramp:
        case IRNodeType::Call:
        case IRNodeType::Let:
        case IRNodeType::Shuffle:
        case IRNodeType::VectorReduce:
            internal_error << "Unreachable";
            break;
        case IRNodeType::LetStmt:
            return ((T *)this)->visit((const LetStmt *)node, std::forward<Args>(args)...);
        case IRNodeType::AssertStmt:
            return ((T *)this)->visit((const AssertStmt *)node, std::forward<Args>(args)...);
        case IRNodeType::ProducerConsumer:
            return ((T *)this)->visit((const ProducerConsumer *)node, std::forward<Args>(args)...);
        case IRNodeType::For:
            return ((T *)this)->visit((const For *)node, std::forward<Args>(args)...);
        case IRNodeType::Acquire:
            return ((T *)this)->visit((const Acquire *)node, std::forward<Args>(args)...);
        case IRNodeType::Store:
            return ((T *)this)->visit((const Store *)node, std::forward<Args>(args)...);
        case IRNodeType::Provide:
            return ((T *)this)->visit((const Provide *)node, std::forward<Args>(args)...);
        case IRNodeType::Allocate:
            return ((T *)this)->visit((const Allocate *)node, std::forward<Args>(args)...);
        case IRNodeType::Free:
            return ((T *)this)->visit((const Free *)node, std::forward<Args>(args)...);
        case IRNodeType::Realize:
            return ((T *)this)->visit((const Realize *)node, std::forward<Args>(args)...);
        case IRNodeType::Block:
            return ((T *)this)->visit((const Block *)node, std::forward<Args>(args)...);
        case IRNodeType::Fork:
            return ((T *)this)->visit((const Fork *)node, std::forward<Args>(args)...);
        case IRNodeType::IfThenElse:
            return ((T *)this)->visit((const IfThenElse *)node, std::forward<Args>(args)...);
        case IRNodeType::Evaluate:
            return ((T *)this)->visit((const Evaluate *)node, std::forward<Args>(args)...);
        case IRNodeType::Prefetch:
            return ((T *)this)->visit((const Prefetch *)node, std::forward<Args>(args)...);
        case IRNodeType::Atomic:
            return ((T *)this)->visit((const Atomic *)node, std::forward<Args>(args)...);
        case IRNodeType::HoistedStorage:
            return ((T *)this)->visit((const HoistedStorage *)node, std::forward<Args>(args)...);
        }
        return StmtRet{};
    }

public:
    template<typename... Args>
    HALIDE_ALWAYS_INLINE StmtRet dispatch(const Stmt &s, Args &&...args) {
        return dispatch_stmt(s.get(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_ALWAYS_INLINE StmtRet dispatch(Stmt &&s, Args &&...args) {
        return dispatch_stmt(s.get(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_ALWAYS_INLINE ExprRet dispatch(const Expr &e, Args &&...args) {
        return dispatch_expr(e.get(), std::forward<Args>(args)...);
    }

    template<typename... Args>
    HALIDE_ALWAYS_INLINE ExprRet dispatch(Expr &&e, Args &&...args) {
        return dispatch_expr(e.get(), std::forward<Args>(args)...);
    }
};

}  // namespace Internal
}  // namespace Halide

#endif

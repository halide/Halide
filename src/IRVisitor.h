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

#define HALIDE_IR_VISITOR_VISIT(kind, type) virtual void visit(const type *);
    HALIDE_IR_NODE_X(HALIDE_IR_VISITOR_VISIT)
#undef HALIDE_IR_VISITOR_VISIT
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
    /** The nodes visited so far. Only includes nodes with a ref count greater
     * than one, because we know that nodes with a ref count of 1 will only be
     * visited once if their parents are only visited once. */
    std::set<const IRNode *> visited;

protected:
    /** These methods should call 'include' on the children to only
     * visit them if they haven't been visited already. */
    // @{
#define HALIDE_IR_GRAPH_VISITOR_VISIT(kind, type) void visit(const type *) override;
    HALIDE_IR_NODE_X(HALIDE_IR_GRAPH_VISITOR_VISIT)
#undef HALIDE_IR_GRAPH_VISITOR_VISIT
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

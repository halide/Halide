#ifndef HALIDE_IR_VISITOR_H
#define HALIDE_IR_VISITOR_H

#include <set>

#include "CompilerProfiling.h"
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

    inline void profiled_visit(const Stmt &s) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        s.accept(this);
    }

    inline void profiled_visit(const Expr &e) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        e.accept(this);
    }

    template<typename T>
    inline void profiled_visit(const T *op) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        visit(op);
    }

protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement accept())
    template<typename T>
    friend struct ExprNode;

    template<typename T>
    friend struct StmtNode;

#define HALIDE_DECL_VISIT(T) \
    virtual void visit(const T *op);
    HALIDE_FOR_EACH_IR_NODE(HALIDE_DECL_VISIT)
#undef HALIDE_DECL_VISIT
};

/** A lambda-based IR visitor that accepts multiple lambdas for different
 * node types. */
template<typename... Lambdas>
struct LambdaVisitor final : IRVisitor {
    explicit LambdaVisitor(Lambdas... lambdas)
        : handlers(std::move(lambdas)...) {
    }

    /** Public helper to call the base visitor from lambdas. */
    template<typename T>
    void visit_base(const T *op) {
        IRVisitor::visit(op);
    }

private:
    LambdaOverloads<Lambdas...> handlers;

    template<typename T>
    auto visit_impl(const T *op) {
        // Catch lambdas that accidentally take non-const T* (e.g. For *
        // instead of const For *). Such lambdas silently never match.
        static_assert(!std::is_invocable_v<decltype(handlers), LambdaVisitor *, T *> ||
                          std::is_invocable_v<decltype(handlers), LambdaVisitor *, const T *>,
                      "visit_with lambda takes a non-const node pointer; use const T * instead");
        if constexpr (std::is_invocable_v<decltype(handlers), LambdaVisitor *, const T *>) {
            return handlers(this, op);
        } else {
            return this->visit_base(op);
        }
    }

protected:
#define HALIDE_CALL_VISIT_EXPR_IMPL(T)                                          \
    void visit(const T *op) override {                                          \
        ZoneScopedVisitor(IRNodeType::T, "LambdaVisitor", Profiling::BIT_EXPR); \
        this->visit_impl(op);                                                   \
    }
    HALIDE_FOR_EACH_IR_EXPR(HALIDE_CALL_VISIT_EXPR_IMPL)
#undef HALIDE_CALL_VISIT_EXPR_IMPL
#define HALIDE_CALL_VISIT_STMT_IMPL(T)                                          \
    void visit(const T *op) override {                                          \
        ZoneScopedVisitor(IRNodeType::T, "LambdaVisitor", Profiling::BIT_STMT); \
        this->visit_impl(op);                                                   \
    }
    HALIDE_FOR_EACH_IR_STMT(HALIDE_CALL_VISIT_STMT_IMPL)
#undef HALIDE_CALL_VISIT_STMT_IMPL
};

template<typename... Lambdas>
void visit_with(const IRNode *ir, Lambdas &&...lambdas) {
    LambdaVisitor visitor{std::forward<Lambdas>(lambdas)...};
    // Each lambda must take two args: (auto *self, <some-pointer> op).
    // Test with const IntImm * (works for auto * params via deduction) and
    // nullptr_t (works for specific-type params via implicit conversion).
    constexpr bool all_take_two_args =
        ((std::is_invocable_v<Lambdas, decltype(&visitor), const IntImm *> ||
          std::is_invocable_v<Lambdas, decltype(&visitor), decltype(nullptr)>) &&
         ...);
    static_assert(all_take_two_args,
                  "All visit_with lambdas must take two arguments: (auto *self, const T *op)");
    ir->accept(&visitor);
}

template<typename... Lambdas>
void visit_with(const IRHandle &ir, Lambdas &&...lambdas) {
    visit_with(ir.get(), std::forward<Lambdas>(lambdas)...);
}

/** A base class for algorithms that walk recursively over the IR
 * without visiting the same node twice. This is for passes that are
 * capable of interpreting the IR as a DAG instead of a tree. */
class IRGraphVisitor : public IRVisitor {
public:
    /** By default these methods add the node to the visited set, and
     * return whether or not it was already there. If it wasn't there,
     * it delegates to the appropriate visit method. You can override
     * them if you like. */
    // @{
    virtual void include(const Expr &);
    virtual void include(const Stmt &);
    // @}

    inline void profiled_include(const Expr &e) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        include(e);
    }
    inline void profiled_include(const Stmt &s) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        include(s);
    }

private:
    /** The nodes visited so far. Only includes nodes with a ref count greater
     * than one, because we know that nodes with a ref count of 1 will only be
     * visited once if their parents are only visited once. */
    std::set<const IRNode *> visited;

protected:
    /** These methods should call 'include' on the children to only
     * visit them if they haven't been visited already. */
    // @{
#define HALIDE_VISIT_OVERRIDE(T) \
    void visit(const T *) override;
    HALIDE_FOR_EACH_IR_NODE(HALIDE_VISIT_OVERRIDE)
#undef HALIDE_VISIT_OVERRIDE
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
#ifdef WITH_COMPILER_PROFILING
#ifdef HALIDE_ENABLE_RTTI
    const char *name = typeid(T).name();
#else
    const char *name = "VariadicVisitor";
#endif
#endif

    template<typename... Args>
    ExprRet dispatch_expr(const BaseExprNode *node, Args &&...args) {
        if (node == nullptr) {
            return ExprRet{};
        }
        ZoneScopedVisitor(node->node_type, name, Profiling::BIT_EXPR);
        switch (node->node_type) {
#define HALIDE_SWITCH_EXPR(NT) \
    case IRNodeType::NT:       \
        return ((T *)this)->visit((const NT *)node, std::forward<Args>(args)...);
            HALIDE_FOR_EACH_IR_EXPR(HALIDE_SWITCH_EXPR)
#undef HALIDE_SWITCH_EXPR

        default:
            internal_error << "Unreachable";
        }
        return ExprRet{};
    }

    template<typename... Args>
    StmtRet dispatch_stmt(const BaseStmtNode *node, Args &&...args) {
        if (node == nullptr) {
            return StmtRet{};
        }
        ZoneScopedVisitor(node->node_type, name, Profiling::BIT_STMT);
        switch (node->node_type) {
#define HALIDE_SWITCH_STMT(NT) \
    case IRNodeType::NT:       \
        return ((T *)this)->visit((const NT *)node, std::forward<Args>(args)...);
            HALIDE_FOR_EACH_IR_STMT(HALIDE_SWITCH_STMT)
#undef HALIDE_SWITCH_STMT

        default:
            internal_error << "Unreachable";
            break;
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

#ifndef HALIDE_IR_MUTATOR_H
#define HALIDE_IR_MUTATOR_H

/** \file
 * Defines a base class for passes over the IR that modify it
 */

#include <map>
#include <type_traits>
#include <utility>

#include "CompilerProfiling.h"
#include "IR.h"

namespace Halide {
namespace Internal {

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
class IRMutator {
public:
    IRMutator() = default;
    virtual ~IRMutator() = default;

    /** This is the main interface for using a mutator. Also call
     * these in your subclass to mutate sub-expressions and
     * sub-statements.
     */
    virtual Expr mutate(const Expr &expr);
    virtual Stmt mutate(const Stmt &stmt);

    inline Expr operator()(const Expr &expr) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        return mutate(expr);
    }

    inline Stmt operator()(const Stmt &stmt) {
        ZoneScopedN(HalideVisitorDynamicNameTag);
        return mutate(stmt);
    }

    // Mutate all the Exprs and return the new list in ret, along with
    // a flag that is true iff at least one item in the list changed.
    std::pair<std::vector<Expr>, bool> mutate_with_changes(const std::vector<Expr> &);

    // Like mutate_with_changes, but discard the changes flag.
    std::vector<Expr> mutate(const std::vector<Expr> &exprs) {
        return mutate_with_changes(exprs).first;
    }

protected:
    // ExprNode<> and StmtNode<> are allowed to call visit (to implement mutate_expr/mutate_stmt())
    template<typename T>
    friend struct ExprNode;
    template<typename T>
    friend struct StmtNode;

#define HALIDE_DECL_VISIT_EXPR(T) \
    virtual Expr visit(const T *op);
    HALIDE_FOR_EACH_IR_EXPR(HALIDE_DECL_VISIT_EXPR)
#undef HALIDE_DECL_VISIT_EXPR

#define HALIDE_DECL_VISIT_STMT(T) \
    virtual Stmt visit(const T *op);
    HALIDE_FOR_EACH_IR_STMT(HALIDE_DECL_VISIT_STMT)
#undef HALIDE_DECL_VISIT_STMT
};

/** A mutator that caches and reapplies previously done mutations so
 * that it can handle graphs of IR that have not had CSE done to
 * them. */
class IRGraphMutator : public IRMutator {
protected:
    std::map<Expr, Expr, ExprCompare> expr_replacements;
    std::map<Stmt, Stmt, Stmt::Compare> stmt_replacements;

public:
    using IRMutator::mutate;
    Stmt mutate(const Stmt &s) override;
    Expr mutate(const Expr &e) override;
};

/** A lambda-based IR mutator that accepts multiple lambdas for different
 * node types. */
template<typename... Lambdas>
struct LambdaMutator final : IRMutator {
    explicit LambdaMutator(Lambdas... lambdas)
        : handlers(std::move(lambdas)...) {
    }

    /** Public helper to call the base visitor from lambdas. */
    template<typename T>
    auto visit_base(const T *op) {
        return IRMutator::visit(op);
    }

private:
    LambdaOverloads<Lambdas...> handlers;

    template<typename T>
    auto visit_impl(const T *op) {
        // Catch lambdas that accidentally take non-const T* (e.g. For *
        // instead of const For *). Such lambdas silently never match.
        static_assert(!std::is_invocable_v<decltype(handlers), LambdaMutator *, T *> ||
                          std::is_invocable_v<decltype(handlers), LambdaMutator *, const T *>,
                      "mutate_with lambda takes a non-const node pointer; use const T * instead");
        if constexpr (std::is_invocable_v<decltype(handlers), LambdaMutator *, const T *>) {
            return handlers(this, op);
        } else {
            return this->visit_base(op);
        }
    }

protected:
#define HALIDE_CALL_VISIT_EXPR_IMPL(T)                                          \
    Expr visit(const T *op) override {                                          \
        ZoneScopedVisitor(IRNodeType::T, "LambdaMutator", Profiling::BIT_EXPR); \
        return this->visit_impl(op);                                            \
    }
    HALIDE_FOR_EACH_IR_EXPR(HALIDE_CALL_VISIT_EXPR_IMPL)
#undef HALIDE_CALL_VISIT_EXPR_IMPL
#define HALIDE_CALL_VISIT_STMT_IMPL(T)                                          \
    Stmt visit(const T *op) override {                                          \
        ZoneScopedVisitor(IRNodeType::T, "LambdaMutator", Profiling::BIT_STMT); \
        return this->visit_impl(op);                                            \
    }
    HALIDE_FOR_EACH_IR_STMT(HALIDE_CALL_VISIT_STMT_IMPL)
#undef HALIDE_CALL_VISIT_STMT_IMPL
};

/** A lambda-based IR mutator that accepts multiple lambdas for overloading
 * the base mutate() method. */
template<typename... Lambdas>
struct LambdaMutatorGeneric final : IRMutator {
    explicit LambdaMutatorGeneric(Lambdas... lambdas)
        : handlers(std::move(lambdas)...) {
    }

    /** Public helper to call the base mutator from lambdas. */
    // Note: C++26 introduces variadic friends: https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p2893r3.html
    // So the mutate_base API could be replaced with:
    //     friend Lambdas...;
    template<typename T>
    auto mutate_base(const T &op) {
        return IRMutator::mutate(op);
    }

    Expr mutate(const Expr &e) override {
        ZoneScopedVisitor(e, "LambdaMutatorGeneric");
        if constexpr (std::is_invocable_v<decltype(handlers), LambdaMutatorGeneric *, const Expr &>) {
            return handlers(this, e);
        } else {
            return this->mutate_base(e);
        }
    }

    Stmt mutate(const Stmt &e) override {
        ZoneScopedVisitor(e, "LambdaMutatorGeneric");
        if constexpr (std::is_invocable_v<decltype(handlers), LambdaMutatorGeneric *, const Stmt &>) {
            return handlers(this, e);
        } else {
            return this->mutate_base(e);
        }
    }

private:
    LambdaOverloads<Lambdas...> handlers;
};

template<typename T, typename... Lambdas>
auto mutate_with(const T &ir, Lambdas &&...lambdas) {
    using Overloads = LambdaOverloads<Lambdas...>;
    using Generic = LambdaMutatorGeneric<Overloads>;
    if constexpr (std::is_invocable_v<Overloads, Generic *, const Expr &> ||
                  std::is_invocable_v<Overloads, Generic *, const Stmt &>) {
        return LambdaMutatorGeneric{std::forward<Lambdas>(lambdas)...}(ir);
    } else {
        LambdaMutator mutator{std::forward<Lambdas>(lambdas)...};
        // Each lambda must take two args: (auto *self, <some-pointer> op).
        // Test with const IntImm * (works for auto * params via deduction) and
        // nullptr_t (works for specific-type params via implicit conversion).
        constexpr bool all_take_two_args =
            ((std::is_invocable_v<Lambdas, decltype(&mutator), const IntImm *> ||
              std::is_invocable_v<Lambdas, decltype(&mutator), decltype(nullptr)>) &&
             ...);
        static_assert(all_take_two_args,
                      "All mutate_with lambdas must take two arguments: (auto *self, const T *op)");
        return mutator(ir);
    }
}

template<typename... Lambdas>
auto mutate_with(const IRNode *ir, Lambdas &&...lambdas) -> IRHandle {
    if (ir->node_type <= StrongestExprNodeType) {
        return mutate_with(Expr((const BaseExprNode *)ir), std::forward<Lambdas>(lambdas)...);
    } else {
        return mutate_with(Stmt((const BaseStmtNode *)ir), std::forward<Lambdas>(lambdas)...);
    }
}

/** A helper function for mutator-like things to mutate regions */
template<typename Mutator, typename... Args>
std::pair<Region, bool> mutate_region(Mutator *mutator, const Region &bounds, Args &&...args) {
    Region new_bounds(bounds.size());
    bool bounds_changed = false;

    for (size_t i = 0; i < bounds.size(); i++) {
        Expr old_min = bounds[i].min;
        Expr old_extent = bounds[i].extent;
        Expr new_min = mutator->mutate(old_min, args...);
        Expr new_extent = mutator->mutate(old_extent, args...);
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

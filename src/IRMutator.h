#ifndef HALIDE_IR_MUTATOR_H
#define HALIDE_IR_MUTATOR_H

/** \file
 * Defines a base class for passes over the IR that modify it
 */

#include <map>
#include <type_traits>
#include <utility>

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

    virtual Expr visit(const IntImm *);
    virtual Expr visit(const UIntImm *);
    virtual Expr visit(const FloatImm *);
    virtual Expr visit(const StringImm *);
    virtual Expr visit(const Cast *);
    virtual Expr visit(const Reinterpret *);
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
    virtual Expr visit(const Let *);
    virtual Stmt visit(const LetStmt *);
    virtual Stmt visit(const AssertStmt *);
    virtual Stmt visit(const ProducerConsumer *);
    virtual Stmt visit(const Store *);
    virtual Stmt visit(const Provide *);
    virtual Stmt visit(const Allocate *);
    virtual Stmt visit(const Free *);
    virtual Stmt visit(const Realize *);
    virtual Stmt visit(const Block *);
    virtual Stmt visit(const Fork *);
    virtual Stmt visit(const IfThenElse *);
    virtual Stmt visit(const Evaluate *);
    virtual Expr visit(const Call *);
    virtual Expr visit(const Variable *);
    virtual Stmt visit(const For *);
    virtual Stmt visit(const Acquire *);
    virtual Expr visit(const Shuffle *);
    virtual Stmt visit(const Prefetch *);
    virtual Stmt visit(const HoistedStorage *);
    virtual Stmt visit(const Atomic *);
    virtual Expr visit(const VectorReduce *);
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

template<typename Derived>
struct LambdaMutatorBase : IRMutator {
    /** Public helper to call the base visitor from lambdas. */
    template<typename T>
    auto visit_base(const T *op) {
        return IRMutator::visit(op);
    }

protected:
#define HALIDE_LAMBDA_MUTATOR_VISIT(kind, type)              \
    kind visit(const type *op) override {                    \
        return static_cast<Derived *>(this)->visit_impl(op); \
    }
    HALIDE_IR_NODE_X(HALIDE_LAMBDA_MUTATOR_VISIT)
#undef HALIDE_LAMBDA_MUTATOR_VISIT
};

template<typename... Ts>
struct LambdaOverloads : Ts... {
    using Ts::operator()...;
    explicit LambdaOverloads(Ts... ts)
        : Ts(std::move(ts))... {
    }
};

/** A lambda-based IR mutator that accepts multiple lambdas for different
 * node types. */
template<typename... Lambdas>
struct LambdaMutator final : LambdaMutatorBase<LambdaMutator<Lambdas...>> {
    explicit LambdaMutator(Lambdas... lambdas)
        : handlers(std::move(lambdas)...) {
    }

private:
    LambdaOverloads<Lambdas...> handlers;

    // Make LambdaMutatorBase a friend so it can call visit_impl
    friend struct LambdaMutatorBase<LambdaMutator>;

    template<typename T>
    auto visit_impl(const T *op) {
        if constexpr (std::is_invocable_v<decltype(handlers), const T *, LambdaMutator *>) {
            return handlers(op, this);
        } else {
            return this->visit_base(op);
        }
    }
};

template<typename T, typename... Lambdas>
auto mutate_with(const T &ir, Lambdas &&...lambdas) {
    return LambdaMutator{std::forward<Lambdas>(lambdas)...}.mutate(ir);
}

/** A lambda-based IR mutator that accepts a single generic lambda that
 * handles all node types via if-constexpr. */
template<typename Lambda>
struct GenericLambdaMutator final : LambdaMutatorBase<GenericLambdaMutator<Lambda>> {
    explicit GenericLambdaMutator(Lambda lambda)
        : handler(std::move(lambda)) {
    }

private:
    Lambda handler;

    // Make LambdaMutatorBase a friend so it can call visit_impl
    friend struct LambdaMutatorBase<GenericLambdaMutator>;

    template<typename T>
    auto visit_impl(const T *op) {
        return handler(op, this);
    }
};

template<typename T, typename Lambda>
auto mutate_with_generic(const T &ir, Lambda &&lambda) {
    return GenericLambdaMutator{std::forward<Lambda>(lambda)}.mutate(ir);
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

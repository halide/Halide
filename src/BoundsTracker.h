#ifndef HALIDE_BOUNDS_TRACKER_H
#define HALIDE_BOUNDS_TRACKER_H

/** \file
 * A utility for finding constant bounds of expressions at some point inside
 * a Stmt tree.
 */

#include <string>
#include <utility>
#include <vector>

#include "Bounds.h"
#include "Expr.h"
#include "Scope.h"

namespace Halide {
namespace Internal {

/** Accumulates the bounds-relevant context available at some point inside a
 * Stmt tree -- enclosing pure LetStmt/Let bindings and For loop ranges -- as
 * a mutator or visitor descends, and uses it to find constant bounds for
 * expressions at that point far more reliably than a bare
 * find_constant_bound() call.
 *
 * In addition to the usual scope-based lookup (cheap, but only sees a bound
 * if every intermediate variable it passes through was itself pushed with an
 * already-constant bound), find_constant_bound_aggressive() falls back to
 * literally wrapping an expression in all enclosing pure lets, inlining
 * them, and re-simplifying. This is the trick bound_constant_extent_loops
 * has always used to find constant loop extents, generalized so other
 * passes that infer constant bounds (e.g. BoundSmallAllocations,
 * AllocationBoundsInference) can use it too.
 */
class BoundsTracker {
public:
    /** An RAII binding produced by push_for/push_let. Pops everything it
     * pushed when destroyed. */
    class Binding {
    public:
        Binding() = default;
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;
        Binding(Binding &&other) noexcept;
        ~Binding();

    private:
        friend class BoundsTracker;
        Binding(BoundsTracker *tracker, ScopedBinding<Interval> scope_binding, bool recorded_let);

        BoundsTracker *tracker = nullptr;
        ScopedBinding<Interval> scope_binding;
        bool recorded_let = false;
    };

    /** Push the bounds of a for loop variable: the envelope [lower bound of
     * min, upper bound of max], each resolved against everything pushed so
     * far. */
    Binding push_for(const std::string &name, const Expr &min, const Expr &max);

    /** Push a let binding. Always updates the fast-path scope with a
     * constant-bounds estimate of the value. Additionally records the
     * syntactic binding for find_constant_bound_aggressive()'s slow path,
     * but only if the value is pure -- substituting an impure expression
     * into multiple places would change its meaning. */
    Binding push_let(const std::string &name, const Expr &value);

    /** An RAII guard produced by push_fact. Pops the fact when destroyed. */
    class FactGuard {
    public:
        FactGuard() = default;
        FactGuard(const FactGuard &) = delete;
        FactGuard &operator=(const FactGuard &) = delete;
        FactGuard(FactGuard &&other) noexcept;
        ~FactGuard();

    private:
        friend class BoundsTracker;
        explicit FactGuard(BoundsTracker *tracker);

        BoundsTracker *tracker = nullptr;
    };

    /** Push a condition known to be true at this point (e.g. because we're
     * in the then-case of an IfThenElse that tests it, or it's the
     * condition of a dominating assert). Used as a simplifier assumption by
     * the slow path in find_constant_bound_aggressive(). Doesn't affect the
     * fast-path scope. */
    FactGuard push_fact(const Expr &condition);

    /** Fast path only: find a constant bound using the current scope. See
     * find_constant_bound() in Bounds.h. */
    Expr find_constant_bound(const Expr &e, Direction d) const;
    Interval find_constant_bounds(const Expr &e) const;

    /** Fast path first; on failure, wrap e in all pending pure lets
     * (producing a self-contained copy with no free references to enclosing
     * lets), inline them with substitute_in_all_lets, and simplify before
     * retrying against the resulting expression and the scope. More
     * expensive than find_constant_bound(), but succeeds far more often,
     * because the simplifier can cancel terms across let boundaries that
     * interval arithmetic through opaque variable lookups cannot. */
    Expr find_constant_bound_aggressive(const Expr &e, Direction d) const;
    Interval find_constant_bounds_aggressive(const Expr &e) const;

    /** Wrap e in all pending pure lets, inline them with
     * substitute_in_all_lets, and simplify under the current scope and
     * dominating facts. Unlike find_constant_bound_aggressive(), the result
     * need not be a constant -- this is just a plain simplify() call that
     * can see context (enclosing let values, dominating conditions) that a
     * caller holding only a bare Expr has no way to pass in. Useful for
     * expressions built by context-free helpers (e.g. box_touched) whose
     * result ends up referencing variables bound by lets enclosing the
     * point the helper was called from. */
    Expr simplify_with_context(const Expr &e) const;

private:
    Scope<Interval> scope;
    std::vector<std::pair<std::string, Expr>> lets;
    std::vector<Expr> facts;
};

}  // namespace Internal
}  // namespace Halide

#endif

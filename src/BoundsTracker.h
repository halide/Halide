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
        Binding(BoundsTracker *tracker, ScopedBinding<Interval> scope_binding, bool recorded_let,
                bool recorded_loop = false);

        BoundsTracker *tracker = nullptr;
        ScopedBinding<Interval> scope_binding;
        bool recorded_let = false;
        bool recorded_loop = false;
    };

    /** Push the bounds of a for loop variable: the envelope [lower bound of
     * min, upper bound of max], each resolved against everything pushed so
     * far. Additionally records the range symbolically, so that
     * find_constant_bounds_aggressive() can substitute the endpoints into an
     * expression that turns out to be monotonic in the loop variable. That
     * recovers bounds the constants-only scope can't represent, because a
     * loop's min or max may itself mention a symbol the expression also
     * mentions. */
    Binding push_for(const std::string &name, const Expr &min, const Expr &max);

    /** Push an already-computed Interval directly, bypassing derivation.
     * Useful when a caller has proven a tighter bound for a variable already
     * in scope (e.g. because a dominating conditional narrows it) and wants
     * to temporarily refine it. Does not participate in
     * find_constant_bound_aggressive()'s let-substitution. */
    Binding push_interval(const std::string &name, const Interval &interval);

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

    /** The current fast-path scope of constant bounds, for passes that need
     * to feed it directly into simplify() or a similar helper that accepts a
     * Scope<Interval> of assumptions, rather than going through
     * find_constant_bound(). Note that Simplify's own internal bounds
     * representation is constant-only anyway (it converts via as_const_int
     * at ingestion), so this scope -- itself always constant-or-unbounded --
     * loses nothing for that use case. It is not, however, suitable for
     * general symbolic interval arithmetic (e.g. bounds_of_expr_in_scope)
     * where a non-constant symbolic bound would otherwise be useful. */
    const Scope<Interval> &interval_scope() const {
        return scope;
    }

    /** The dominating conditions currently known to hold (see push_fact()),
     * for passes that want to feed them directly into simplify() as
     * assumptions alongside interval_scope(), without paying for the more
     * expensive wrap-in-every-pending-let-and-resimplify path that
     * find_constant_bound_aggressive()/simplify_with_context() use. */
    const std::vector<Expr> &known_facts() const {
        return facts;
    }

private:
    /** Tighten an interval by exploiting monotonicity in an enclosing loop
     * variable: if e is monotonic in it, e's extremes over the loop are
     * reached at the ends of the loop's range, so substituting the symbolic
     * endpoints and constant-bounding the results can succeed where
     * per-node interval arithmetic can't, because substitution keeps a
     * symbol shared by e and the loop's range correlated. */
    Interval tighten_using_loop_monotonicity(const Expr &e, Interval interval) const;

    struct LoopRange {
        std::string name;
        Expr min, max;
    };

    Scope<Interval> scope;
    std::vector<std::pair<std::string, Expr>> lets;
    std::vector<Expr> facts;
    std::vector<LoopRange> loops;
};

}  // namespace Internal
}  // namespace Halide

#endif

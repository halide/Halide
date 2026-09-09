#ifndef HALIDE_BOUNDS_TRACKER_H
#define HALIDE_BOUNDS_TRACKER_H

/** \file
 * A utility for finding constant bounds of expressions at some point inside
 * a Stmt tree.
 */

#include <set>
#include <string>
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
 *
 * Pushing a binding is deliberately cheap: it only records the Expr. None of
 * the derived information -- the constant bounds of a let's value, whether
 * it's pure, the symbolic facts implied by a loop's range -- is computed
 * until something actually asks a question. Passes that walk a whole tree
 * but only query at a handful of nodes therefore pay almost nothing for the
 * bindings they descend through. Once computed, the derived information is
 * memoized for as long as the binding is alive, so repeated queries under
 * the same bindings don't redo the work.
 */
class BoundsTracker {
public:
    /** An RAII binding produced by push_for/push_let/push_interval. Pops
     * what it pushed when destroyed. */
    class Binding {
    public:
        Binding() = default;
        Binding(const Binding &) = delete;
        Binding &operator=(const Binding &) = delete;
        Binding(Binding &&other) noexcept;
        ~Binding();

    private:
        friend class BoundsTracker;
        explicit Binding(BoundsTracker *tracker);

        BoundsTracker *tracker = nullptr;
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

    /** Push a let binding. Feeds both the fast-path scope (via a constant
     * bounds estimate of the value) and find_constant_bound_aggressive()'s
     * slow path (via the syntactic binding, but only if the value turns out
     * to be pure -- substituting an impure expression into multiple places
     * would change its meaning). Both of those are derived lazily. */
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
     * retrying against the resulting expression and the scope. Any endpoint
     * still missing after that is attempted once more by substituting the
     * range of an enclosing loop e is monotonic in. More expensive than
     * find_constant_bound(), but succeeds far more often, because the
     * simplifier can cancel terms across let boundaries that interval
     * arithmetic through opaque variable lookups cannot.
     *
     * The single-Direction form is a wrapper around the Interval form; it
     * returns an undefined Expr if no bound in that direction was found. */
    Interval find_constant_bounds_aggressive(const Expr &e) const;
    Expr find_constant_bound_aggressive(const Expr &e, Direction d) const;

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
     * where a non-constant symbolic bound would otherwise be useful.
     *
     * Asking for the scope forces every pending binding to be evaluated, so
     * prefer find_constant_bound() where it suffices. */
    const Scope<Interval> &interval_scope() const;

    /** The dominating conditions that could possibly say something about e --
     * those passed to push_fact(), plus the range of every enclosing loop --
     * for passes that want to feed them directly into simplify() as
     * assumptions alongside interval_scope(), without paying for the more
     * expensive wrap-in-every-pending-let-and-resimplify path that
     * find_constant_bound_aggressive()/simplify_with_context() use. Facts
     * sharing no variable (even transitively, via another fact) with e are
     * dropped: they can't help, and the simplifier pays to ingest each one. */
    std::vector<Expr> relevant_facts(const Expr &e) const;

private:
    /** Tighten an interval by exploiting monotonicity in an enclosing loop
     * variable: if e is monotonic in it, e's extremes over the loop are
     * reached at the ends of the loop's range, so substituting the symbolic
     * endpoints and constant-bounding the results can succeed where
     * per-node interval arithmetic can't, because substitution keeps a
     * symbol shared by e and the loop's range correlated. */
    Interval tighten_using_loop_monotonicity(const Expr &e, Interval interval) const;

    /** Wrap e in the enclosing pure lets it refers to, innermost first,
     * skipping those it can't reach. Returns the names it ended up
     * mentioning in *used, so a caller can go on to pick out the facts that
     * could say something about it. */
    Expr wrap_in_used_lets(const Expr &e, std::set<std::string> *used) const;

    /** The facts mentioning any name in *used, growing *used with the names
     * each selected fact brings in, until it reaches a fixed point. */
    std::vector<Expr> facts_mentioning(std::set<std::string> *used) const;

    enum class Kind {
        Let,      //< lo is the value
        Loop,     //< lo and hi are the loop min and max
        Explicit  //< bounds is given up front
    };

    struct Entry {
        Kind kind;
        std::string name;
        Expr lo, hi;
        /** Only meaningful once this entry has been realized into `scope`,
         * i.e. once its index is below `realized`. */
        Interval bounds;
        /** Whether the bound Exprs are pure, and so safe to substitute into
         * multiple places. Computed on first use. */
        enum class Purity {
            Unknown,
            Pure,
            Impure
        };
        mutable Purity purity = Purity::Unknown;
    };

    /** Realize every entry not yet reflected in `scope`, outermost first, so
     * that each one's bounds are derived in the scope of the entries that
     * enclose it. */
    void realize_all() const;
    Interval bounds_of(const Entry &entry) const;
    bool entry_is_pure(const Entry &entry) const;
    void pop_entry();
    void pop_fact();

    /** The bindings in scope, outermost first. Mutable because realizing an
     * entry -- a pure memoization of what the entry already implies -- has
     * to be possible from the const query methods. */
    mutable std::vector<Entry> entries;

    /** entries[0, realized) have been pushed into `scope`, in that order. */
    mutable size_t realized = 0;
    mutable Scope<Interval> scope;

    /** The conditions passed to push_fact(). Those implied by enclosing
     * loops are materialized on demand by relevant_facts(). */
    std::vector<Expr> facts;
};

}  // namespace Internal
}  // namespace Halide

#endif

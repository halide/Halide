#include "BoundsTracker.h"

#include "ExprUsesVar.h"
#include "IR.h"
#include "IROperator.h"
#include "Monotonic.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

BoundsTracker::Binding::Binding(BoundsTracker *tracker, ScopedBinding<Interval> scope_binding, bool recorded_let,
                                bool recorded_loop)
    : tracker(tracker), scope_binding(std::move(scope_binding)), recorded_let(recorded_let),
      recorded_loop(recorded_loop) {
}

BoundsTracker::Binding::Binding(Binding &&other) noexcept
    : tracker(other.tracker),
      scope_binding(std::move(other.scope_binding)),
      recorded_let(other.recorded_let),
      recorded_loop(other.recorded_loop) {
    other.recorded_let = false;
    other.recorded_loop = false;
}

BoundsTracker::Binding::~Binding() {
    if (recorded_let) {
        tracker->lets.pop_back();
    }
    if (recorded_loop) {
        tracker->loops.pop_back();
        tracker->facts.pop_back();
        tracker->facts.pop_back();
    }
}

BoundsTracker::Binding BoundsTracker::push_for(const std::string &name, const Expr &min, const Expr &max) {
    Interval min_bounds = find_constant_bounds(min);
    Interval max_bounds = find_constant_bounds(max);
    Interval b = Interval::make_union(min_bounds, max_bounds);
    b.min = simplify(b.min);
    b.max = simplify(b.max);

    // Also record the range symbolically. The scope above can only hold
    // constants, so it drops any relationship between the loop variable and a
    // symbol appearing in its min or max (e.g. the tile index of a split being
    // bounded by a ceiling-divide of the extent being split).
    bool recorded_loop = false;
    if (min.type() == Int(32) && max.type() == Int(32) && is_pure(min) && is_pure(max)) {
        loops.push_back(LoopRange{name, min, max});
        Expr loop_var = Variable::make(Int(32), name);
        facts.push_back(loop_var >= min);
        facts.push_back(loop_var <= max);
        recorded_loop = true;
    }
    return Binding(this, ScopedBinding<Interval>(scope, name, b), false, recorded_loop);
}

BoundsTracker::Binding BoundsTracker::push_interval(const std::string &name, const Interval &interval) {
    return Binding(this, ScopedBinding<Interval>(scope, name, interval), false);
}

BoundsTracker::Binding BoundsTracker::push_let(const std::string &name, const Expr &value) {
    bool pure = is_pure(value);
    if (pure) {
        lets.emplace_back(name, value);
    }
    return Binding(this, ScopedBinding<Interval>(scope, name, find_constant_bounds(value)), pure);
}

BoundsTracker::FactGuard::FactGuard(BoundsTracker *tracker)
    : tracker(tracker) {
}

BoundsTracker::FactGuard::FactGuard(FactGuard &&other) noexcept
    : tracker(other.tracker) {
    other.tracker = nullptr;
}

BoundsTracker::FactGuard::~FactGuard() {
    if (tracker) {
        tracker->facts.pop_back();
    }
}

BoundsTracker::FactGuard BoundsTracker::push_fact(const Expr &condition) {
    facts.push_back(condition);
    return FactGuard(this);
}

Expr BoundsTracker::find_constant_bound(const Expr &e, Direction d) const {
    return Halide::Internal::find_constant_bound(e, d, scope);
}

Interval BoundsTracker::find_constant_bounds(const Expr &e) const {
    return Halide::Internal::find_constant_bounds(e, scope);
}

Expr BoundsTracker::simplify_with_context(const Expr &e) const {
    Expr wrapped = e;
    for (const auto &[name, value] : reverse_view(lets)) {
        wrapped = Let::make(name, value, wrapped);
    }
    wrapped = remove_likelies(wrapped);
    wrapped = substitute_in_all_lets(wrapped);
    // Deliberately pass an empty bounds scope here, not `scope`: mixing a
    // bounds scope with equality facts can make the simplifier represent a
    // variable by its (wide) interval instead of substituting the exact
    // value an equality fact implies, which weakens the very reasoning this
    // call exists to do. Any tightening from `scope` happens afterward, once
    // this expression is no longer being asked to prove an equality.
    debug(4) << "Simplify with context: " << wrapped << "\n";
    for (const auto &fact : facts) {
        debug(4) << " [fact] " << fact << "\n";
    }
    wrapped = Halide::Internal::simplify(wrapped, Scope<Interval>::empty_scope(),
                                         Scope<ModulusRemainder>::empty_scope(), facts);

    // Now that the lets are inlined, a dependence on an enclosing loop
    // variable may appear on both sides of a subtraction. Cancel those out
    // and simplify again -- the facts recorded by push_for can only relate
    // the loop variable to the rest of the expression once the expression
    // mentions it directly. This can grow the expression, so only keep the
    // result if it actually bought us something.
    Expr cancelled = bound_correlated_differences(wrapped);
    if (!cancelled.same_as(wrapped)) {
        cancelled = Halide::Internal::simplify(cancelled, Scope<Interval>::empty_scope(),
                                               Scope<ModulusRemainder>::empty_scope(), facts);
        if (is_const(cancelled) || cancelled.node_type() < wrapped.node_type()) {
            wrapped = cancelled;
        }
    }
    return wrapped;
}

Expr BoundsTracker::find_constant_bound_aggressive(const Expr &e, Direction d) const {
    Expr bound = find_constant_bound(e, d);
    if (bound.defined()) {
        return bound;
    }
    Expr wrapped = simplify_with_context(e);
    return Halide::Internal::find_constant_bound(wrapped, d, scope);
}

Interval BoundsTracker::tighten_using_loop_monotonicity(const Expr &e, Interval interval) const {
    if (e.type() != Int(32)) {
        return interval;
    }
    // Innermost first: the tightest correlation is usually with the nearest
    // enclosing loop.
    for (const LoopRange &loop : reverse_view(loops)) {
        if (interval.has_lower_bound() && interval.has_upper_bound()) {
            break;
        }
        if (!expr_uses_var(e, loop.name)) {
            continue;
        }
        Monotonic m = is_monotonic(e, loop.name);
        Expr at_lower, at_upper;
        if (m == Monotonic::Increasing || m == Monotonic::Constant) {
            at_lower = loop.min;
            at_upper = loop.max;
        } else if (m == Monotonic::Decreasing) {
            at_lower = loop.max;
            at_upper = loop.min;
        } else {
            continue;
        }
        if (!interval.has_lower_bound()) {
            Expr lo = simplify(substitute(loop.name, at_lower, e));
            interval.min = Halide::Internal::find_constant_bounds(lo, scope).min;
        }
        if (!interval.has_upper_bound()) {
            Expr hi = simplify(substitute(loop.name, at_upper, e));
            interval.max = Halide::Internal::find_constant_bounds(hi, scope).max;
        }
    }
    return interval;
}

Interval BoundsTracker::find_constant_bounds_aggressive(const Expr &e) const {
    Interval interval = find_constant_bounds(e);
    if (interval.has_lower_bound() && interval.has_upper_bound()) {
        return interval;
    }
    Expr wrapped = simplify_with_context(e);
    interval = Halide::Internal::find_constant_bounds(wrapped, scope);
    if (interval.has_lower_bound() && interval.has_upper_bound()) {
        return interval;
    }
    return tighten_using_loop_monotonicity(wrapped, interval);
}

}  // namespace Internal
}  // namespace Halide

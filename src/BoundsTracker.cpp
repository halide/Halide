#include "BoundsTracker.h"

#include "IR.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

BoundsTracker::Binding::Binding(BoundsTracker *tracker, ScopedBinding<Interval> scope_binding, bool recorded_let)
    : tracker(tracker), scope_binding(std::move(scope_binding)), recorded_let(recorded_let) {
}

BoundsTracker::Binding::Binding(Binding &&other) noexcept
    : tracker(other.tracker),
      scope_binding(std::move(other.scope_binding)),
      recorded_let(other.recorded_let) {
    other.recorded_let = false;
}

BoundsTracker::Binding::~Binding() {
    if (recorded_let) {
        tracker->lets.pop_back();
    }
}

BoundsTracker::Binding BoundsTracker::push_for(const std::string &name, const Expr &min, const Expr &max) {
    Interval min_bounds = find_constant_bounds(min);
    Interval max_bounds = find_constant_bounds(max);
    Interval b = Interval::make_union(min_bounds, max_bounds);
    b.min = simplify(b.min);
    b.max = simplify(b.max);
    return Binding(this, ScopedBinding<Interval>(scope, name, b), false);
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
    wrapped = Halide::Internal::simplify(wrapped, Scope<Interval>::empty_scope(),
                                         Scope<ModulusRemainder>::empty_scope(), facts);
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

Interval BoundsTracker::find_constant_bounds_aggressive(const Expr &e) const {
    Interval interval = find_constant_bounds(e);
    if (interval.has_lower_bound() && interval.has_upper_bound()) {
        return interval;
    }
    Expr wrapped = simplify_with_context(e);
    return Halide::Internal::find_constant_bounds(wrapped, scope);
}

}  // namespace Internal
}  // namespace Halide

#include "BoundsTracker.h"

#include "ExprUsesVar.h"
#include "IR.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Monotonic.h"
#include "Simplify.h"
#include "SimplifyCorrelatedDifferences.h"
#include "Substitute.h"
#include "Util.h"

namespace Halide {
namespace Internal {

namespace {

/** Every name an Expr refers to, whether as a variable or as a buffer. */
class CollectUsedNames : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    void visit(const Variable *op) override {
        names->insert(op->name);
    }

    void visit(const Load *op) override {
        names->insert(op->name);
        IRGraphVisitor::visit(op);
    }

    void visit(const Store *op) override {
        names->insert(op->name);
        IRGraphVisitor::visit(op);
    }

public:
    std::set<std::string> *names;

    explicit CollectUsedNames(std::set<std::string> *names)
        : names(names) {
    }
};

bool mentions_any(const Expr &e, const std::set<std::string> &names) {
    std::set<std::string> used;
    CollectUsedNames collect(&used);
    e.accept(&collect);
    for (const std::string &name : used) {
        if (names.count(name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

BoundsTracker::Binding::Binding(BoundsTracker *tracker)
    : tracker(tracker) {
}

BoundsTracker::Binding::Binding(Binding &&other) noexcept
    : tracker(other.tracker) {
    other.tracker = nullptr;
}

BoundsTracker::Binding::~Binding() {
    if (tracker) {
        tracker->pop_entry();
    }
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
        tracker->pop_fact();
    }
}

BoundsTracker::Binding BoundsTracker::push_for(const std::string &name, const Expr &min, const Expr &max) {
    entries.push_back(Entry{Kind::Loop, name, min, max});
    return Binding(this);
}

BoundsTracker::Binding BoundsTracker::push_interval(const std::string &name, const Interval &interval) {
    entries.push_back(Entry{Kind::Explicit, name, Expr(), Expr(), interval});
    return Binding(this);
}

BoundsTracker::Binding BoundsTracker::push_let(const std::string &name, const Expr &value) {
    entries.push_back(Entry{Kind::Let, name, value});
    return Binding(this);
}

BoundsTracker::FactGuard BoundsTracker::push_fact(const Expr &condition) {
    facts.push_back(condition);
    return FactGuard(this);
}

void BoundsTracker::pop_entry() {
    internal_assert(!entries.empty());
    if (realized == entries.size()) {
        scope.pop(entries.back().name);
        realized--;
    }
    entries.pop_back();
}

void BoundsTracker::pop_fact() {
    internal_assert(!facts.empty());
    facts.pop_back();
}

bool BoundsTracker::entry_is_pure(const Entry &entry) const {
    if (entry.purity == Entry::Purity::Unknown) {
        bool pure = (!entry.lo.defined() || is_pure(entry.lo)) &&
                    (!entry.hi.defined() || is_pure(entry.hi));
        entry.purity = pure ? Entry::Purity::Pure : Entry::Purity::Impure;
    }
    return entry.purity == Entry::Purity::Pure;
}

Interval BoundsTracker::bounds_of(const Entry &entry) const {
    switch (entry.kind) {
    case Kind::Let:
        return Halide::Internal::find_constant_bounds(entry.lo, scope);
    case Kind::Loop: {
        Interval b = Interval::make_union(Halide::Internal::find_constant_bounds(entry.lo, scope),
                                          Halide::Internal::find_constant_bounds(entry.hi, scope));
        b.min = simplify(b.min);
        b.max = simplify(b.max);
        return b;
    }
    case Kind::Explicit:
        return entry.bounds;
    }
    return Interval::everything();
}

void BoundsTracker::realize_all() const {
    // Outermost first: each entry's bounds may depend on the ones enclosing
    // it, and pushing in this order also keeps the scope's shadowing of
    // repeated names matching the tree.
    for (; realized < entries.size(); realized++) {
        Entry &entry = entries[realized];
        entry.bounds = bounds_of(entry);
        scope.push(entry.name, entry.bounds);
    }
}

const Scope<Interval> &BoundsTracker::interval_scope() const {
    realize_all();
    return scope;
}

Expr BoundsTracker::wrap_in_used_lets(const Expr &e, std::set<std::string> *used) const {
    CollectUsedNames collect(used);
    e.accept(&collect);
    Expr result = e;
    // Innermost first, so that a let picked up along the way can still pull in
    // the outer lets its value refers to. Conservatively treats every name the
    // body mentions as a possible reference to an enclosing let, even where an
    // inner let shadows it.
    for (const Entry &entry : reverse_view(entries)) {
        if (entry.kind == Kind::Let && used->count(entry.name) && entry_is_pure(entry)) {
            entry.lo.accept(&collect);
            result = Let::make(entry.name, entry.lo, result);
        }
    }
    return result;
}

std::vector<Expr> BoundsTracker::facts_mentioning(std::set<std::string> *used) const {
    std::vector<Expr> result;
    std::vector<Expr> candidates = facts;
    for (const Entry &entry : entries) {
        // The scope can only hold constants, so it drops any relationship
        // between a loop variable and a symbol appearing in its min or max
        // (e.g. the tile index of a split being bounded by a ceiling-divide of
        // the extent being split). Record the range symbolically as well.
        if (entry.kind == Kind::Loop &&
            entry.lo.type() == Int(32) && entry.hi.type() == Int(32) &&
            entry_is_pure(entry)) {
            Expr loop_var = Variable::make(Int(32), entry.name);
            candidates.push_back(loop_var >= entry.lo);
            candidates.push_back(loop_var <= entry.hi);
        }
    }

    // A fact that shares no variable with what we're asking about can't say
    // anything about it, but a fact it does share one with can bring in
    // variables that make a third fact relevant, so keep sweeping until
    // nothing new is picked up.
    bool changed = true;
    while (changed) {
        changed = false;
        for (Expr &candidate : candidates) {
            if (candidate.defined() && mentions_any(candidate, *used)) {
                CollectUsedNames collect(used);
                candidate.accept(&collect);
                result.push_back(candidate);
                candidate = Expr();
                changed = true;
            }
        }
    }
    return result;
}

std::vector<Expr> BoundsTracker::relevant_facts(const Expr &e) const {
    std::set<std::string> used;
    CollectUsedNames collect(&used);
    e.accept(&collect);
    return facts_mentioning(&used);
}

Expr BoundsTracker::find_constant_bound(const Expr &e, Direction d) const {
    return Halide::Internal::find_constant_bound(e, d, interval_scope());
}

Interval BoundsTracker::find_constant_bounds(const Expr &e) const {
    return Halide::Internal::find_constant_bounds(e, interval_scope());
}

Expr BoundsTracker::simplify_with_context(const Expr &e) const {
    std::set<std::string> used;
    Expr wrapped = wrap_in_used_lets(e, &used);
    wrapped = remove_likelies(wrapped);
    wrapped = substitute_in_all_lets(wrapped);
    std::vector<Expr> relevant = facts_mentioning(&used);
    // Deliberately pass an empty bounds scope here, not `scope`: mixing a
    // bounds scope with equality facts can make the simplifier represent a
    // variable by its (wide) interval instead of substituting the exact
    // value an equality fact implies, which weakens the very reasoning this
    // call exists to do. Any tightening from `scope` happens afterward, once
    // this expression is no longer being asked to prove an equality.
    debug(4) << "Simplify with context: " << wrapped << "\n";
    for (const Expr &fact : relevant) {
        debug(4) << " [fact] " << fact << "\n";
    }
    wrapped = Halide::Internal::simplify(wrapped, Scope<Interval>::empty_scope(),
                                         Scope<ModulusRemainder>::empty_scope(), relevant);

    // Now that the lets are inlined, a dependence on an enclosing loop
    // variable may appear on both sides of a subtraction. Cancel those out
    // and simplify again -- the facts recorded by push_for can only relate
    // the loop variable to the rest of the expression once the expression
    // mentions it directly. This can grow the expression, so only keep the
    // result if it actually bought us something.
    Expr cancelled = bound_correlated_differences(wrapped);
    if (!cancelled.same_as(wrapped)) {
        cancelled = Halide::Internal::simplify(cancelled, Scope<Interval>::empty_scope(),
                                               Scope<ModulusRemainder>::empty_scope(), relevant);
        if (is_const(cancelled) || cancelled.node_type() < wrapped.node_type()) {
            wrapped = cancelled;
        }
    }
    return wrapped;
}

Interval BoundsTracker::tighten_using_loop_monotonicity(const Expr &e, Interval interval) const {
    if (e.type() != Int(32)) {
        return interval;
    }
    // Innermost first: the tightest correlation is usually with the nearest
    // enclosing loop.
    for (const Entry &entry : reverse_view(entries)) {
        if (interval.has_lower_bound() && interval.has_upper_bound()) {
            break;
        }
        if (entry.kind != Kind::Loop ||
            entry.lo.type() != Int(32) || entry.hi.type() != Int(32) ||
            !entry_is_pure(entry) ||
            !expr_uses_var(e, entry.name)) {
            continue;
        }
        Monotonic m = is_monotonic(e, entry.name);
        Expr at_lower, at_upper;
        if (m == Monotonic::Increasing || m == Monotonic::Constant) {
            at_lower = entry.lo;
            at_upper = entry.hi;
        } else if (m == Monotonic::Decreasing) {
            at_lower = entry.hi;
            at_upper = entry.lo;
        } else {
            continue;
        }
        if (!interval.has_lower_bound()) {
            Expr lo = simplify(substitute(entry.name, at_lower, e));
            interval.min = Halide::Internal::find_constant_bounds(lo, scope).min;
        }
        if (!interval.has_upper_bound()) {
            Expr hi = simplify(substitute(entry.name, at_upper, e));
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

Expr BoundsTracker::find_constant_bound_aggressive(const Expr &e, Direction d) const {
    Interval interval = find_constant_bounds_aggressive(e);
    if (d == Direction::Lower) {
        return interval.has_lower_bound() ? interval.min : Expr();
    } else {
        return interval.has_upper_bound() ? interval.max : Expr();
    }
}

}  // namespace Internal
}  // namespace Halide

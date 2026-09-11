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

/** Every variable an Expr mentions, as the hash a Variable node carries. */
class CollectVarHashes : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    void visit(const Variable *op) override {
        out->push_back(op->hash);
    }

public:
    std::vector<uint32_t> *out;

    explicit CollectVarHashes(std::vector<uint32_t> *out)
        : out(out) {
    }
};

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
    facts_version++;
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
    facts.push_back(Fact{condition});
    facts_version++;
    return FactGuard(this);
}

void BoundsTracker::pop_entry() {
    internal_assert(!entries.empty());
    if (realized == entries.size()) {
        scope.pop(entries.back().name);
        realized--;
    }
    if (entries.back().kind == Kind::Loop) {
        facts_version++;
    }
    entries.pop_back();
}

void BoundsTracker::pop_fact() {
    internal_assert(!facts.empty());
    facts.pop_back();
    facts_version++;
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
        // A pure let gets inlined by wrap_in_used_lets before anything asks
        // for a bound, and bounding the expression that results is both
        // cheaper and sharper than bounding the value here and looking it up
        // through an opaque variable. It still has to occupy a slot in the
        // scope, though: a let shadowing an outer variable that does have a
        // bound must hide it, not let lookups fall through to it. An impure
        // let is never inlined, so it needs a real bound.
        if (entry_is_pure(entry)) {
            return Interval::everything();
        }
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

Expr BoundsTracker::wrap_in_used_lets(const Expr &e) const {
    std::set<std::string> used;
    CollectUsedNames collect(&used);
    e.accept(&collect);
    Expr result = e;
    // Innermost first, so that a let picked up along the way can still pull in
    // the outer lets its value refers to. Conservatively treats every name the
    // body mentions as a possible reference to an enclosing let, even where an
    // inner let shadows it.
    for (const Entry &entry : reverse_view(entries)) {
        if (entry.kind == Kind::Let && used.count(entry.name) && entry_is_pure(entry)) {
            entry.lo.accept(&collect);
            result = Let::make(entry.name, entry.lo, result);
        }
    }
    return result;
}

BoundsTracker::VarSet BoundsTracker::vars_of(const Expr &e) {
    VarSet result;
    CollectVarHashes collect(&result.hashes);
    e.accept(&collect);
    for (uint32_t h : result.hashes) {
        result.bloom |= (uint64_t)1 << (h & 63);
    }
    result.sort_unique();
    return result;
}

const Expr &BoundsTracker::candidate_expr(const Candidate &c) const {
    const Entry &entry = entries[c.entry];
    return c.use_hi ? entry.fact_hi : entry.fact_lo;
}

const BoundsTracker::VarSet &BoundsTracker::candidate_vars(const Candidate &c) const {
    return entries[c.entry].fact_vars;
}

const std::vector<BoundsTracker::Candidate> &BoundsTracker::candidates() const {
    if (candidates_version == facts_version) {
        return all_candidates;
    }
    all_candidates.clear();
    for (size_t i = 0; i < entries.size(); i++) {
        const Entry &entry = entries[i];
        if (entry.kind != Kind::Loop ||
            entry.lo.type() != Int(32) || entry.hi.type() != Int(32) ||
            !entry_is_pure(entry)) {
            continue;
        }
        if (!entry.facts_built) {
            // The scope can only hold constants, so it drops any
            // relationship between a loop variable and a symbol appearing in
            // its min or max (e.g. the tile index of a split being bounded
            // by a ceiling-divide of the extent being split). Record the
            // range symbolically too. One variable summary covers both ends:
            // over-estimating what a condition mentions only costs an extra
            // condition handed to the simplifier.
            Expr loop_var = Variable::make(Int(32), entry.name);
            entry.fact_lo = loop_var >= entry.lo;
            entry.fact_hi = loop_var <= entry.hi;
            entry.fact_vars = vars_of(entry.fact_lo);
            entry.fact_vars.merge(vars_of(entry.fact_hi));
            entry.facts_built = true;
        }
        all_candidates.push_back(Candidate{i, false});
        all_candidates.push_back(Candidate{i, true});
    }
    candidates_version = facts_version;
    return all_candidates;
}

std::vector<Expr> BoundsTracker::relevant_facts(const Expr &e) const {
    std::vector<Expr> result;
    const std::vector<Candidate> &cands = candidates();
    if (cands.empty() && facts.empty()) {
        return result;
    }

    VarSet used = vars_of(e);

    // A condition sharing no variable with what we're asking about can't say
    // anything about it, but one that does can bring in variables that make
    // a third condition relevant, so keep sweeping until nothing new is
    // picked up.
    std::vector<bool> taken_fact(facts.size(), false);
    std::vector<bool> taken(cands.size(), false);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < facts.size(); i++) {
            const Fact &fact = facts[i];
            if (taken_fact[i]) {
                continue;
            }
            if (!fact.vars_built) {
                fact.vars = vars_of(fact.condition);
                fact.vars_built = true;
            }
            if (!fact.vars.intersects(used)) {
                continue;
            }
            taken_fact[i] = true;
            result.push_back(fact.condition);
            used.merge(fact.vars);
            changed = true;
        }
        for (size_t i = 0; i < cands.size(); i++) {
            if (taken[i] || !candidate_vars(cands[i]).intersects(used)) {
                continue;
            }
            taken[i] = true;
            result.push_back(candidate_expr(cands[i]));
            used.merge(candidate_vars(cands[i]));
            changed = true;
        }
    }
    return result;
}

Expr BoundsTracker::find_constant_bound(const Expr &e, Direction d) const {
    return Halide::Internal::find_constant_bound(e, d, interval_scope());
}

Interval BoundsTracker::find_constant_bounds(const Expr &e) const {
    return Halide::Internal::find_constant_bounds(e, interval_scope());
}

Expr BoundsTracker::simplify_with_context(const Expr &e) const {
    Expr wrapped = wrap_in_used_lets(e);
    wrapped = remove_likelies(wrapped);
    // Leave the lets standing and let simplify() pick the ones worth
    // inlining. Inlining them all up front repeats each value at every use,
    // which costs nothing in the resulting graph but a great deal in the tree
    // the simplifier actually walks, since it does not memoize on the shared
    // nodes -- a few hundred nodes of graph can become tens of thousands of
    // nodes of walk.
    std::vector<Expr> relevant = relevant_facts(e);
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

    // A dependence on an enclosing loop variable may now appear on both sides
    // of a subtraction. Cancel those out and simplify again -- the facts
    // recorded by push_for can only relate the loop variable to the rest of
    // the expression once the expression mentions it directly. This can grow
    // the expression, so only keep the result if it actually bought us
    // something.
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

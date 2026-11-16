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

std::optional<int64_t> as_int64(const Expr &e) {
    if (auto i = as_const_int(e)) {
        return i;
    }
    if (auto u = as_const_uint(e); u && *u <= (uint64_t)INT64_MAX) {
        return (int64_t)*u;
    }
    return std::nullopt;
}

/** The constant endpoints of an Interval from find_constant_bounds() in
 * Bounds.h. */
ConstantInterval to_constant_interval(const Interval &interval) {
    ConstantInterval result;
    if (auto lo = interval.has_lower_bound() ? as_int64(interval.min) : std::nullopt) {
        result.min_defined = true;
        result.min = *lo;
    }
    if (auto hi = interval.has_upper_bound() ? as_int64(interval.max) : std::nullopt) {
        result.max_defined = true;
        result.max = *hi;
    }
    return result;
}

std::optional<int64_t> bound_in_direction(const ConstantInterval &interval, Direction d) {
    if (d == Direction::Lower) {
        return interval.min_defined ? std::optional<int64_t>(interval.min) : std::nullopt;
    } else {
        return interval.max_defined ? std::optional<int64_t>(interval.max) : std::nullopt;
    }
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
    facts.push_back(Fact{condition});
    return FactGuard(this);
}

void BoundsTracker::pop_entry() {
    internal_assert(!entries.empty());
    if (realized == entries.size()) {
        if (entries.back().in_scope) {
            scope.pop(entries.back().name);
        }
        realized--;
    }
    if (candidates_built == entries.size()) {
        size_t idx = entries.size() - 1;
        while (!all_candidates.empty() && all_candidates.back().entry == idx) {
            all_candidates.pop_back();
        }
        candidates_built--;
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
        // A pure let is substituted by wrap_in_used_lets instead, which
        // gives sharper bounds. An impure let is not, so it needs a bound.
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
    // Outermost first, since an entry's bounds depend on the ones enclosing
    // it.
    for (; realized < entries.size(); realized++) {
        Entry &entry = entries[realized];
        entry.bounds = bounds_of(entry);
        // An unbounded binding reads the same as no binding, so it only
        // needs a slot to hide an enclosing one of the same name.
        entry.in_scope = !entry.bounds.is_everything() || scope.contains(entry.name);
        if (entry.in_scope) {
            scope.push(entry.name, entry.bounds);
        }
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
    // Innermost first, so a let's value can pull in the outer lets it uses.
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
        result.bloom |= VarSet::bloom_bit(h);
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
    for (; candidates_built < entries.size(); candidates_built++) {
        size_t i = candidates_built;
        const Entry &entry = entries[i];
        if (entry.kind != Kind::Loop ||
            entry.lo.type() != Int(32) || entry.hi.type() != Int(32) ||
            !entry_is_pure(entry)) {
            continue;
        }
        if (!entry.facts_built) {
            // The scope only holds constants, so also keep the range
            // symbolically, for when min or max mention other variables.
            Expr loop_var = Variable::make(Int(32), entry.name);
            // The simplifier only accepts facts as LT/LE, not GT/GE.
            entry.fact_lo = entry.lo <= loop_var;
            entry.fact_hi = loop_var <= entry.hi;
            entry.fact_vars = vars_of(entry.fact_lo);
            entry.fact_vars.merge(vars_of(entry.fact_hi));
            entry.facts_built = true;
        }
        all_candidates.push_back(Candidate{i, false});
        all_candidates.push_back(Candidate{i, true});
    }
    return all_candidates;
}

std::vector<Expr> BoundsTracker::relevant_facts(const Expr &e) const {
    std::vector<Expr> result;
    const std::vector<Candidate> &cands = candidates();
    if (cands.empty() && facts.empty()) {
        return result;
    }

    VarSet used = vars_of(e);

    // Sweep until no new fact is taken. After the first sweep, only a fact
    // sharing a Bloom bit with one taken in the previous sweep can be new.
    std::vector<bool> taken_fact(facts.size(), false);
    std::vector<bool> taken(cands.size(), false);
    uint64_t fresh = (uint64_t)-1;
    bool changed = true;
    while (changed) {
        changed = false;
        uint64_t next_fresh = 0;
        for (size_t i = 0; i < facts.size(); i++) {
            const Fact &fact = facts[i];
            if (taken_fact[i]) {
                continue;
            }
            if (!fact.vars_built) {
                fact.condition = simplify(fact.condition);
                fact.vars = vars_of(fact.condition);
                fact.vars_built = true;
            }
            if (!(fact.vars.bloom & fresh) || !fact.vars.intersects(used)) {
                continue;
            }
            taken_fact[i] = true;
            result.push_back(fact.condition);
            next_fresh |= fact.vars.bloom;
            used.merge(fact.vars);
            changed = true;
        }
        for (size_t i = 0; i < cands.size(); i++) {
            if (taken[i]) {
                continue;
            }
            const VarSet &vars = candidate_vars(cands[i]);
            if (!(vars.bloom & fresh) || !vars.intersects(used)) {
                continue;
            }
            taken[i] = true;
            result.push_back(candidate_expr(cands[i]));
            next_fresh |= vars.bloom;
            used.merge(vars);
            changed = true;
        }
        fresh = next_fresh;
    }
    return result;
}

ConstantInterval BoundsTracker::find_constant_bounds_in_scope(const Expr &e) const {
    return to_constant_interval(Halide::Internal::find_constant_bounds(e, interval_scope()));
}

Expr BoundsTracker::simplify_with_context(const Expr &e) const {
    Expr wrapped = wrap_in_used_lets(e);
    wrapped = remove_likelies(wrapped);
    // Leave the lets for simplify() to inline: inlining them all up front
    // can blow up the tree it walks.
    std::vector<Expr> relevant = relevant_facts(e);
    // No bounds scope here: with one, the simplifier can use a variable's
    // interval where an equality fact gives its exact value.
    debug(4) << "Simplify with context: " << wrapped << "\n";
    for (const Expr &fact : relevant) {
        debug(4) << " [fact] " << fact << "\n";
    }
    wrapped = Halide::Internal::simplify(wrapped, Scope<Interval>::empty_scope(),
                                         Scope<ModulusRemainder>::empty_scope(), relevant);

    // Cancel correlated differences, which can expose loop variables to the
    // loop facts. This can grow the expression, so keep it only if it helps.
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

ConstantInterval BoundsTracker::tighten_using_loop_monotonicity(const Expr &e, ConstantInterval interval) const {
    if (e.type() != Int(32)) {
        return interval;
    }
    // Innermost first: the nearest loop usually correlates best.
    for (const Entry &entry : reverse_view(entries)) {
        if (interval.is_single_point()) {
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
        {
            Expr lo = simplify(substitute(entry.name, at_lower, e));
            ConstantInterval b = to_constant_interval(Halide::Internal::find_constant_bounds(lo, scope));
            if (b.min_defined) {
                interval.intersect_if_nonempty(ConstantInterval::bounded_below(b.min));
            }
        }
        {
            Expr hi = simplify(substitute(entry.name, at_upper, e));
            ConstantInterval b = to_constant_interval(Halide::Internal::find_constant_bounds(hi, scope));
            if (b.max_defined) {
                interval.intersect_if_nonempty(ConstantInterval::bounded_above(b.max));
            }
        }
    }
    return interval;
}

ConstantInterval BoundsTracker::find_constant_bounds(const Expr &e) const {
    ConstantInterval interval = find_constant_bounds_in_scope(e);
    // A bounded result can still be loose, so keep going until it is exact.
    if (interval.is_single_point()) {
        return interval;
    }
    Expr wrapped = simplify_with_context(e);
    interval.intersect_if_nonempty(
        to_constant_interval(Halide::Internal::find_constant_bounds(wrapped, scope)));
    if (interval.is_single_point()) {
        return interval;
    }
    return tighten_using_loop_monotonicity(wrapped, interval);
}

std::optional<int64_t> BoundsTracker::find_constant_bound(const Expr &e, Direction d) const {
    return bound_in_direction(find_constant_bounds(e), d);
}

}  // namespace Internal
}  // namespace Halide

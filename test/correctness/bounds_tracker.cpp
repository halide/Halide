#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Note: this test is built with NDEBUG, so assert() compiles to nothing.
bool check(bool ok, const char *msg) {
    if (!ok) {
        printf("Failed: %s\n", msg);
    }
    return ok;
}

bool is_exactly(const ConstantInterval &i, int64_t v) {
    return i.is_single_point(v);
}

bool is_range(const ConstantInterval &i, int64_t lo, int64_t hi) {
    return i.min_defined && i.max_defined && i.min == lo && i.max == hi;
}

Expr var(const char *name) {
    return Variable::make(Int(32), name);
}

// The constant bounds of a let value reach every use of the let.
bool test_let_bounds() {
    BoundsTracker tracker;
    auto a = tracker.push_let("a", 3);
    return check(is_exactly(tracker.find_constant_bounds(var("a") + 2), 5),
                 "a let with a constant value bounds its uses");
}

// Interval arithmetic sees a let only through its bounds, so it can't cancel
// a variable against itself across a let boundary. Wrapping the expression in
// the lets it uses lets the simplifier do it.
bool test_let_substitution() {
    BoundsTracker tracker;
    auto x = tracker.push_for("x", 0, 10);
    auto a = tracker.push_let("a", var("x"));
    auto b = tracker.push_let("b", var("a") + 4);
    Expr e = var("b") - var("a");

    Interval through_scope = find_constant_bounds(e, tracker.interval_scope());
    bool ok = check(!through_scope.is_single_point(),
                    "interval arithmetic over the scope can't cancel across lets");
    ok &= check(is_exactly(tracker.find_constant_bounds(e), 4),
                "substituting the lets cancels a against itself");
    return ok;
}

// A loop whose range mentions a symbol: the scope can only hold constants, so
// it learns nothing about y. But y - m is monotonic in y, so its extremes are
// at the ends of the loop, where substituting the endpoints cancels m.
bool test_loop_monotonicity() {
    BoundsTracker tracker;
    Expr m = var("m");
    auto y = tracker.push_for("y", m, m + 7);
    Expr e = var("y") - m;

    Interval through_scope = find_constant_bounds(e, tracker.interval_scope());
    bool ok = check(!through_scope.is_bounded(),
                    "the scope can't bound a loop variable with a symbolic range");
    ok &= check(is_range(tracker.find_constant_bounds(e), 0, 7),
                "substituting the loop endpoints bounds y - m");
    return ok;
}

// A dominating condition tells the simplifier which way a select goes. The
// false side is unbounded here, so the scope alone can't bound the select.
// (When the scope alone can, the fact is not consulted: see
// test_tightness_known_limitation.)
bool test_fact() {
    BoundsTracker tracker;
    auto x = tracker.push_for("x", 0, 100);
    Expr e = select(var("x") < 5, 3, var("y"));
    bool ok = check(!tracker.find_constant_bounds(e).is_bounded(),
                    "without the fact, the unbounded side of the select is possible");
    {
        auto fact = tracker.push_fact(var("x") < 5);
        ok &= check(is_exactly(tracker.find_constant_bounds(e), 3),
                    "the fact picks the true side of the select");
    }
    ok &= check(!tracker.find_constant_bounds(e).is_bounded(),
                "the fact is gone once its guard is destroyed");
    return ok;
}

// Known limitation, checked so that fixing it shows up here:
// find_constant_bounds() returns the first result bounded at both ends, and
// only falls back to let substitution, facts and loop monotonicity when the
// scope alone leaves an end unbounded. This is test_loop_monotonicity with m
// bounded by an enclosing loop: the scope now bounds y to [0, 107], so y - m
// comes out as [-100, 107] and the monotonicity step that would give [0, 7]
// never runs. Knowing more about m gives a looser answer. If this check
// fails, find_constant_bounds() has learned to tighten a bounded result: make
// it expect [0, 7], and update the note at BoundsTracker::find_constant_bounds().
bool test_tightness_known_limitation() {
    BoundsTracker tracker;
    Expr m = var("m");
    auto outer = tracker.push_for("m", 0, 100);
    auto y = tracker.push_for("y", m, m + 7);
    return check(is_range(tracker.find_constant_bounds(var("y") - m), -100, 107),
                 "a bounded result is now tightened further: the known limitation "
                 "is fixed, so update this test to expect [0, 7]");
}

// A caller that has proven a tighter range for a variable can push it, and the
// refinement lasts as long as the binding.
bool test_interval() {
    BoundsTracker tracker;
    auto x = tracker.push_for("x", 0, 100);
    Expr e = var("x") * 2;
    bool ok = true;
    {
        auto refined = tracker.push_interval("x", Interval(0, 3));
        ok &= check(is_range(tracker.find_constant_bounds(e), 0, 6),
                    "a pushed interval refines the loop's range");
    }
    ok &= check(is_range(tracker.find_constant_bounds(e), 0, 200),
                "the loop's range is back once the interval is popped");
    return ok;
}

// relevant_facts() keeps the facts that share a variable with the expression,
// directly or through another kept fact, and drops the rest. Here q < 3 only
// becomes relevant once p == q has been taken, which happens after q < 3 was
// first looked at.
bool test_relevant_facts() {
    BoundsTracker tracker;
    Expr p = var("p"), q = var("q"), r = var("r");
    auto f1 = tracker.push_fact(q < 3);
    auto f2 = tracker.push_fact(r > 7);
    auto f3 = tracker.push_fact(p == q);
    std::vector<Expr> facts = tracker.relevant_facts(p + 1);

    auto has = [&](const Expr &fact) {
        for (const Expr &f : facts) {
            if (equal(f, fact)) {
                return true;
            }
        }
        return false;
    };
    bool ok = check(has(p == q), "a fact mentioning p is relevant to p");
    ok &= check(has(q < 3), "a fact reached through p == q is relevant to p");
    ok &= check(!has(r > 7), "a fact sharing no variable with p is dropped");
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    bool ok = true;
    ok &= test_let_bounds();
    ok &= test_let_substitution();
    ok &= test_loop_monotonicity();
    ok &= test_fact();
    ok &= test_tightness_known_limitation();
    ok &= test_interval();
    ok &= test_relevant_facts();
    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

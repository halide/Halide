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

// A let's constant value bounds its uses.
bool test_let_bounds() {
    BoundsTracker tracker;
    auto a = tracker.push_let("a", 3);
    return check(is_exactly(tracker.find_constant_bounds(var("a") + 2), 5),
                 "a let with a constant value bounds its uses");
}

// Substituting the lets lets the simplifier cancel a against itself, which
// interval arithmetic over the scope can't.
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

// The scope can't bound a loop variable with a symbolic range, but
// substituting the loop's ends into y - m, which is monotonic in y, can.
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

// A dominating fact picks the side of a select.
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

    // Here the scope alone bounds the select to [3, 1000].
    Expr bounded = select(var("x") < 5, 3, 1000);
    {
        auto fact = tracker.push_fact(var("x") < 5);
        ok &= check(is_exactly(tracker.find_constant_bounds(bounded), 3),
                    "the fact also tightens a select the scope already bounds");
    }
    return ok;
}

// As test_loop_monotonicity, but with m bounded, so the scope gives a loose
// [-100, 107] that the monotonicity step tightens to [0, 7].
bool test_tighten_bounded_result() {
    BoundsTracker tracker;
    Expr m = var("m");
    auto outer = tracker.push_for("m", 0, 100);
    auto y = tracker.push_for("y", m, m + 7);
    Expr e = var("y") - m;

    Interval through_scope = find_constant_bounds(e, tracker.interval_scope());
    bool ok = check(through_scope.is_bounded() && !is_const(through_scope.min, 0),
                    "the scope alone gives a bounded but loose result");
    ok &= check(is_range(tracker.find_constant_bounds(e), 0, 7),
                "the monotonicity step tightens a bounded result");
    return ok;
}

// A pushed interval narrows a variable until it is popped.
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

// relevant_facts() follows shared variables through other facts: q < 3 is
// only reached through p == q, which comes later in the list.
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
    ok &= test_tighten_bounded_result();
    ok &= test_interval();
    ok &= test_relevant_facts();
    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}

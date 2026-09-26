// Some combinations of tail strategies lower incorrectly when one particular
// split has a tail, i.e. when its factor does not divide the extent it splits.
// For those combinations, Halide requires at runtime that the split has no
// tail. This checks that each combination raises that error when the split
// has a tail, computes the right thing when it doesn't, and that similar
// schedules without the problem are left alone.
#include "Halide.h"
#include "expect_user_error.h"

#include <functional>
#include <sstream>
#include <stdio.h>

using namespace Halide;

namespace {

const char *const reason = "requires the split factor to divide the extent";

// Every combination below is checked twice: with a tail (must fail with the
// precondition error) and without one (must succeed and be correct).
struct Case {
    const char *name;
    std::function<bool(bool with_tail)> run;
};

bool check(const char *name, const Buffer<int> &out, const std::function<int(int, int)> &expected) {
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            if (out(x, y) != expected(x, y)) {
                printf("[%s] FAIL: out(%d, %d) = %d instead of %d\n",
                       name, x, y, out(x, y), expected(x, y));
                return false;
            }
        }
    }
    return true;
}

// A blend stores back a value it loads from the same site, but PredicateStores
// doesn't predicate that load, so in the tail it reads out of bounds.
bool predicate_stores_then_blend(bool with_tail) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Func f("f");
    f(x, y) = x + 16 * y;
    f.split(x, xo, xi, 3, TailStrategy::PredicateStores)
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
        .reorder(xi, yi, xo, yo);
    Buffer<int> out = f.realize({with_tail ? 10 : 9, 7});
    return check("predicate_stores_then_blend", out, [](int x, int y) { return x + 16 * y; });
}

// Without a tail of its own, the PredicateStores split still runs past its
// extent when a split of its outer Var has a tail.
bool predicate_stores_outer_split_then_blend(bool with_tail) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi"), yo("yo"), yi("yi");
    Func f("f");
    f(x, y) = x + 16 * y;
    f.split(x, xo, xi, 3, TailStrategy::PredicateStores)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads)
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
    Buffer<int> out = f.realize({with_tail ? 9 : 12, 7});
    return check("predicate_stores_outer_split_then_blend", out, [](int x, int y) { return x + 16 * y; });
}

bool predicate_stores_then_blend_update(bool with_tail) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Func f("f"), g("g");
    f(x, y) = x + 16 * y;
    f(x, y) += 1;
    g(x, y) = f(x, y);
    // As an intermediate, f is computed over exactly the region g needs.
    f.compute_root()
        .update()
        .split(x, xo, xi, 3, TailStrategy::PredicateStores)
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
    Buffer<int> out = g.realize({with_tail ? 10 : 9, 7});
    return check("predicate_stores_then_blend_update", out, [](int x, int y) { return x + 16 * y + 1; });
}

// Halide issue #9322: bounds inference sizes the producers computed inside a
// PredicateStores split from the predicated stores, but the tail iterations
// still run and read beyond them.
int ignore_trace(JITUserContext *, const halide_trace_event_t *) {
    return 0;
}

bool predicate_stores_compute_at_inner(bool with_tail, bool unroll, bool trace) {
    Var y("y"), yo("yo"), yi("yi");
    Func f0("f0"), f1("f1"), f2("f2"), f3("f3");
    f0(y) = y;
    f1(y) = f0(y - 2) + f0(y + 2);
    f2(y) = f1(y * 2);
    f3(y) = f2(y * 2 - 2) + f2(y * 2 + 2);

    const int extent = with_tail ? 15 : 16;
    f3.bound(y, 0, extent);
    f3.split(y, yo, yi, 4, TailStrategy::PredicateStores);
    f0.compute_root();
    f1.store_root().compute_at(f2, Var::outermost());
    f2.hoist_storage(f3, Var::outermost()).compute_at(f3, yi);
    if (unroll) {
        // With everything unrolled, the simplifier proves the tail's loads
        // out of bounds and deletes the code around them, which must not
        // delete the check.
        f0.unroll(y);
        f1.unroll(y);
        f2.unroll(y);
        f3.unroll(yo).unroll(yi);
    }
    if (trace) {
        // The check must also survive the producers' realizations becoming
        // dead code while their tracing still names their bounds.
        f1.trace_realizations();
        f2.trace_realizations();
        f3.jit_handlers().custom_trace = ignore_trace;
    }

    Buffer<int> out = f3.realize({extent});
    return check("predicate_stores_compute_at_inner", out, [](int y, int) { return 16 * y; });
}

// A split of the outer Var of a ShiftInwardsAndBlend split runs that Var past
// its loop max. The ShiftInwardsAndBlend split shifts those iterations back
// onto its last tile, which RoundUp would then update twice.
bool shift_inwards_and_blend_then_round_up(bool with_tail) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f");
    f(x) = 0;
    f(x) = f(x) + 1;
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, 3, TailStrategy::RoundUp);
    // xo has extent 3 for 20 points, and 4 for 25.
    Buffer<int> out = f.realize({with_tail ? 25 : 20});
    return check("shift_inwards_and_blend_then_round_up", out, [](int, int) { return 1; });
}

bool shift_inwards_and_blend_then_auto(bool with_tail) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f");
    f(x) = 0;
    f(x) = f(x) + 1;
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, 3, TailStrategy::Auto);
    Buffer<int> out = f.realize({with_tail ? 25 : 20});
    return check("shift_inwards_and_blend_then_auto", out, [](int, int) { return 1; });
}

bool shift_inwards_and_blend_then_fused_round_up(bool with_tail) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), t("t"), to("to"), ti("ti");
    Func f("f");
    f(x, y) = 0;
    f(x, y) = f(x, y) + 1;
    // xo is the outer operand of the fuse, so the overhang of t runs xo past
    // its loop max. t has extent 3 * 3 = 9.
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .fuse(y, xo, t)
        .split(t, to, ti, with_tail ? 5 : 3, TailStrategy::RoundUp);
    Buffer<int> out = f.realize({20, 3});
    return check("shift_inwards_and_blend_then_fused_round_up", out, [](int, int) { return 1; });
}

// Here the shifted iterations' loads are predicated off but their stores
// aren't.
bool shift_inwards_then_predicate_loads(bool with_tail) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    f.split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads);
    // xo has extent 3 for 20 points, and 4 for 32.
    Buffer<int> out = f.realize({with_tail ? 20 : 32});
    return check("shift_inwards_then_predicate_loads", out, [](int x, int) { return x + 1; });
}

bool shift_inwards_then_renamed_predicate_loads(bool with_tail) {
    Var x("x"), xo("xo"), xi("xi"), r("r"), ro("ro"), ri("ri");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    f.split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .rename(xo, r)
        .split(r, ro, ri, 2, TailStrategy::PredicateLoads);
    Buffer<int> out = f.realize({with_tail ? 20 : 32});
    return check("shift_inwards_then_renamed_predicate_loads", out, [](int x, int) { return x + 1; });
}

bool shift_inwards_and_blend_then_predicate_loads(bool with_tail) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f"), out("out");
    f(x) = x;
    f(x) = f(x) * 3 + 1;
    out(x) = f(x);
    f.compute_root();
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads)
        .unroll(xoi);
    Buffer<int> r = out.realize({with_tail ? 20 : 32});
    return check("shift_inwards_and_blend_then_predicate_loads", r, [](int x, int) { return 3 * x + 1; });
}

// Schedules that are fine with any tail must not be affected.
bool unaffected(int extent) {
    bool ok = true;
    {
        // The common vectorize-then-unroll pattern: RoundUp on the outer Var
        // of a ShiftInwards split in a pure definition just recomputes.
        Var x("x");
        Func f("f");
        f(x) = x * 2;
        f.vectorize(x, 8).unroll(x, 4);
        ok &= check("vectorize_unroll", f.realize({extent}), [](int x, int) { return x * 2; });
    }
    {
        // PredicateStores on its own, with a producer computed outside the
        // inner Var.
        Var x("x"), xo("xo"), xi("xi");
        Func g("g"), f("f");
        g(x) = x;
        f(x) = g(x - 1) + g(x + 1);
        f.split(x, xo, xi, 4, TailStrategy::PredicateStores);
        g.compute_at(f, xo);
        ok &= check("predicate_stores", f.realize({extent}), [](int x, int) { return 2 * x; });
    }
    {
        // A GuardWithIf split of the outer Var of a ShiftInwardsAndBlend
        // split masks the shifted iterations.
        Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
        Func f("f");
        f(x) = 0;
        f(x) = f(x) + 1;
        f.update()
            .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
            .split(xo, xoo, xoi, 3, TailStrategy::GuardWithIf);
        ok &= check("shift_inwards_and_blend_then_guard_with_if", f.realize({extent}), [](int, int) { return 1; });
    }
    {
        // xo is the inner operand of the fuse, so it stays in range.
        Var x("x"), y("y"), xo("xo"), xi("xi"), t("t"), to("to"), ti("ti");
        Func f("f"), out("out");
        f(x, y) = x + y;
        f(x, y) = f(x, y) * 3 + 1;
        out(x, y) = f(x, y);
        f.compute_root();
        f.update()
            .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
            .fuse(xo, y, t)
            .split(t, to, ti, 5, TailStrategy::PredicateLoads);
        ok &= check("fused_inner", out.realize({extent, 3}), [](int x, int y) { return 3 * (x + y) + 1; });
    }
    {
        // A bound that proves the extent is a multiple of the factor removes
        // the check.
        Var x("x"), xo("xo"), xi("xi"), yo("yo"), yi("yi"), y("y");
        Func f("f");
        f(x, y) = x + 16 * y;
        f.bound(x, 0, 12)
            .split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
        Module m = f.compile_to_module({}, "f");
        bool has_check = false;
        for (const auto &func : m.functions()) {
            std::ostringstream s;
            s << func.body;
            has_check |= s.str().find(reason) != std::string::npos;
        }
        if (has_check) {
            printf("[bounded] FAIL: the check was not removed\n");
            ok = false;
        }
        ok &= check("bounded", f.realize({12, 7}), [](int x, int y) { return x + 16 * y; });
    }
    return ok;
}

}  // namespace

int main(int argc, char **argv) {
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    const Case cases[] = {
        {"predicate_stores_then_blend", predicate_stores_then_blend},
        {"predicate_stores_then_blend_update", predicate_stores_then_blend_update},
        {"predicate_stores_outer_split_then_blend", predicate_stores_outer_split_then_blend},
        {"predicate_stores_compute_at_inner", [](bool t) { return predicate_stores_compute_at_inner(t, false, false); }},
        {"predicate_stores_compute_at_inner_unrolled", [](bool t) { return predicate_stores_compute_at_inner(t, true, false); }},
        {"predicate_stores_compute_at_inner_traced", [](bool t) { return predicate_stores_compute_at_inner(t, false, true); }},
        {"shift_inwards_and_blend_then_round_up", shift_inwards_and_blend_then_round_up},
        {"shift_inwards_and_blend_then_auto", shift_inwards_and_blend_then_auto},
        {"shift_inwards_and_blend_then_fused_round_up", shift_inwards_and_blend_then_fused_round_up},
        {"shift_inwards_then_predicate_loads", shift_inwards_then_predicate_loads},
        {"shift_inwards_then_renamed_predicate_loads", shift_inwards_then_renamed_predicate_loads},
        {"shift_inwards_and_blend_then_predicate_loads", shift_inwards_and_blend_then_predicate_loads},
    };

    int failures = 0;
    for (const Case &c : cases) {
        failures += !expect_user_error(c.name, reason, [&]() { c.run(true); });
        if (c.run(false)) {
            printf("[%s] OK without a tail\n", c.name);
        } else {
            failures++;
        }
    }
    for (int extent : {13, 16, 19, 24, 25}) {
        failures += !unaffected(extent);
    }

    if (failures != 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}

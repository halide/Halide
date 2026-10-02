// Some combinations of tail strategies lower incorrectly when one particular
// split has a tail, i.e. when its factor does not divide the extent it splits.
// Halide forbids those combinations unless the split provably has no tail.
// This checks that each combination is rejected at compile time, that it is
// accepted and computes the right thing when bound() proves there is no tail,
// and that the suggested replacement (GuardWithIf for the offending split) is
// correct with a tail. It also checks that similar schedules without the
// problem, and Auto tail strategies, are left alone.
#include "Halide.h"
#include "expect_user_error.h"

#include <functional>
#include <stdio.h>
#include <vector>

using namespace Halide;

namespace {

const char *const reason = "provably divides the extent";

enum class Variant {
    // As written, with a tail: must be rejected.
    Forbidden,
    // The offending split uses GuardWithIf instead, with a tail.
    GuardWithIf,
    // As written, with bound() proving there is no tail.
    Bounded,
};

struct Built {
    Func out;
    std::vector<int> sizes;
    std::function<int(int, int)> expected;
};

struct Case {
    const char *name;
    std::function<Built(Variant)> build;
};

TailStrategy offending(Variant v, TailStrategy tail) {
    return v == Variant::GuardWithIf ? TailStrategy::GuardWithIf : tail;
}

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
Built predicate_stores_then_blend(Variant v) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Func f("f");
    f(x, y) = x + 16 * y;
    const int w = v == Variant::Bounded ? 9 : 10;
    f.bound(x, 0, w)
        .split(x, xo, xi, 3, offending(v, TailStrategy::PredicateStores))
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend)
        .reorder(xi, yi, xo, yo);
    return {f, {w, 7}, [](int x, int y) { return x + 16 * y; }};
}

// Without a tail of its own, the PredicateStores split still runs past its
// extent when a split of its outer Var has a tail.
Built predicate_stores_outer_split_then_blend(Variant v) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi"), yo("yo"), yi("yi");
    Func f("f");
    f(x, y) = x + 16 * y;
    // xo has extent 3 for 9 points, and 4 for 12.
    const int w = v == Variant::Bounded ? 12 : 9;
    f.bound(x, 0, w)
        .split(x, xo, xi, 3, TailStrategy::PredicateStores)
        .split(xo, xoo, xoi, 2, offending(v, TailStrategy::PredicateLoads))
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
    return {f, {w, 7}, [](int x, int y) { return x + 16 * y; }};
}

Built predicate_stores_then_blend_update(Variant v) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), yo("yo"), yi("yi");
    Func f("f"), g("g");
    f(x, y) = x + 16 * y;
    f(x, y) += 1;
    g(x, y) = f(x, y);
    const int w = v == Variant::Bounded ? 9 : 10;
    // As an intermediate, f is computed over exactly the region g needs.
    f.compute_root().bound(x, 0, w);
    f.update()
        .split(x, xo, xi, 3, offending(v, TailStrategy::PredicateStores))
        .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
    return {g, {w, 7}, [](int x, int y) { return x + 16 * y + 1; }};
}

// Halide issue #9322: bounds inference sizes the producers computed inside a
// PredicateStores split from the predicated stores, but the tail iterations
// still run and read beyond them.
Built predicate_stores_compute_at_inner(Variant v) {
    Var y("y"), yo("yo"), yi("yi");
    Func f0("f0"), f1("f1"), f2("f2"), f3("f3");
    f0(y) = y;
    f1(y) = f0(y - 2) + f0(y + 2);
    f2(y) = f1(y * 2);
    f3(y) = f2(y * 2 - 2) + f2(y * 2 + 2);

    const int extent = v == Variant::Bounded ? 16 : 15;
    f3.bound(y, 0, extent);
    f3.split(y, yo, yi, 4, offending(v, TailStrategy::PredicateStores));
    f0.compute_root();
    f1.store_root().compute_at(f2, Var::outermost());
    f2.hoist_storage(f3, Var::outermost()).compute_at(f3, yi);
    return {f3, {extent}, [](int y, int) { return 16 * y; }};
}

// A split of the outer Var of a ShiftInwardsAndBlend split runs that Var past
// its loop max. The ShiftInwardsAndBlend split shifts those iterations back
// onto its last tile, which RoundUp would then update twice.
Built shift_inwards_and_blend_then_round_up(Variant v) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f");
    f(x) = 0;
    f(x) = f(x) + 1;
    // xo has extent 3 for 24 points, and 4 for 25.
    const int w = v == Variant::Bounded ? 24 : 25;
    f.bound(x, 0, w);
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, 3, offending(v, TailStrategy::RoundUp));
    return {f, {w}, [](int, int) { return 1; }};
}

Built shift_inwards_and_blend_then_fused_round_up(Variant v) {
    Var x("x"), y("y"), xo("xo"), xi("xi"), t("t"), to("to"), ti("ti");
    Func f("f");
    f(x, y) = 0;
    f(x, y) = f(x, y) + 1;
    // xo is the outer operand of the fuse, so the overhang of t runs xo past
    // its loop max. t has extent 3 * 3 = 9.
    f.bound(x, 0, 24).bound(y, 0, 3);
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .fuse(y, xo, t)
        .split(t, to, ti, v == Variant::Bounded ? 3 : 5, offending(v, TailStrategy::RoundUp));
    return {f, {24, 3}, [](int, int) { return 1; }};
}

// Here the shifted iterations' loads are predicated off but their stores
// aren't.
Built shift_inwards_then_predicate_loads(Variant v) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    // xo has extent 3 for 20 points, and 4 for 32.
    const int w = v == Variant::Bounded ? 32 : 20;
    f.bound(x, 0, w)
        .split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .split(xo, xoo, xoi, 2, offending(v, TailStrategy::PredicateLoads));
    return {f, {w}, [](int x, int) { return x + 1; }};
}

Built shift_inwards_then_renamed_predicate_loads(Variant v) {
    Var x("x"), xo("xo"), xi("xi"), r("r"), ro("ro"), ri("ri");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    const int w = v == Variant::Bounded ? 32 : 20;
    f.bound(x, 0, w)
        .split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .rename(xo, r)
        .split(r, ro, ri, 2, offending(v, TailStrategy::PredicateLoads));
    return {f, {w}, [](int x, int) { return x + 1; }};
}

Built shift_inwards_and_blend_then_predicate_loads(Variant v) {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func f("f"), out("out");
    f(x) = x;
    f(x) = f(x) * 3 + 1;
    out(x) = f(x);
    const int w = v == Variant::Bounded ? 32 : 20;
    f.compute_root().bound(x, 0, w);
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .split(xo, xoo, xoi, 2, offending(v, TailStrategy::PredicateLoads))
        .unroll(xoi);
    return {out, {w}, [](int x, int) { return 3 * x + 1; }};
}

// Schedules that are fine with any tail must not be affected, and Auto must
// not pick a forbidden strategy.
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
        // Auto on the outer Var of a ShiftInwardsAndBlend split in an update
        // picks GuardWithIf rather than RoundUp.
        Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
        Func f("f");
        f(x) = 0;
        f(x) = f(x) + 1;
        f.update()
            .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
            .split(xo, xoo, xoi, 3, TailStrategy::Auto);
        ok &= check("shift_inwards_and_blend_then_auto", f.realize({extent}), [](int, int) { return 1; });
    }
    {
        // The same through vectorize and unroll.
        Var x("x");
        Func f("f");
        f(x) = 0;
        f(x) = f(x) + 1;
        f.update().vectorize(x, 8, TailStrategy::ShiftInwardsAndBlend).unroll(x, 3);
        ok &= check("vectorize_blend_unroll", f.realize({extent}), [](int, int) { return 1; });
    }
    {
        // Auto on the outer Var of a PredicateStores split in an update picks
        // GuardWithIf rather than RoundUp. The bound makes the PredicateStores
        // split itself allowed next to the blend.
        Var x("x"), y("y"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi"), yo("yo"), yi("yi");
        Func f("f"), g("g");
        f(x, y) = x + 16 * y;
        f(x, y) += 1;
        g(x, y) = f(x, y);
        f.compute_root().bound(x, 0, 9);
        f.update()
            .split(x, xo, xi, 3, TailStrategy::PredicateStores)
            .split(xo, xoo, xoi, 2, TailStrategy::Auto)
            .split(y, yo, yi, 4, TailStrategy::RoundUpAndBlend);
        ok &= check("predicate_stores_then_auto", g.realize({9, extent}),
                    [](int x, int y) { return x + 16 * y + 1; });
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
        {"predicate_stores_compute_at_inner", predicate_stores_compute_at_inner},
        {"shift_inwards_and_blend_then_round_up", shift_inwards_and_blend_then_round_up},
        {"shift_inwards_and_blend_then_fused_round_up", shift_inwards_and_blend_then_fused_round_up},
        {"shift_inwards_then_predicate_loads", shift_inwards_then_predicate_loads},
        {"shift_inwards_then_renamed_predicate_loads", shift_inwards_then_renamed_predicate_loads},
        {"shift_inwards_and_blend_then_predicate_loads", shift_inwards_and_blend_then_predicate_loads},
    };

    int failures = 0;
    for (const Case &c : cases) {
        // The error must come from compilation, not from running.
        Built p = c.build(Variant::Forbidden);
        failures += !expect_user_error(c.name, reason, [&]() { p.out.compile_jit(); });
        for (Variant v : {Variant::GuardWithIf, Variant::Bounded}) {
            const char *variant = v == Variant::GuardWithIf ? "with GuardWithIf" : "bounded";
            Built q = c.build(v);
            if (check(c.name, q.out.realize(q.sizes), q.expected)) {
                printf("[%s] OK %s\n", c.name, variant);
            } else {
                failures++;
            }
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

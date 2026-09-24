// PredicateLoads on a Var stemming from the outer Var of a ShiftInwards or
// ShiftInwardsAndBlend split must be rejected. PredicateLoads runs the outer
// Var past its loop max, and the earlier split shifts those iterations back
// onto its last tile, inside the realized region. Their loads are predicated
// off but their stores are not, so they would overwrite valid values.
//
// For ShiftInwards that is a recompute with garbage inputs. ShiftInwardsAndBlend
// masks every lane of such an iteration, but the blend still stores the
// predicated-off load of the old value. (Depending on the schedule, loop
// trimming may happen to delete those stores, but e.g. with the inner loop
// unrolled, f(12..19) below would become 0.)
//
// Schedules where the overhang does not reach the shifted split must still be
// accepted and correct.

#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

#if HALIDE_WITH_EXCEPTIONS

using namespace Halide;

namespace {

const char *const reason = "stemming from the outer Var of a prior ShiftInwards or ShiftInwardsAndBlend split";

void scenario_shift_inwards() {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    f.split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads);
    f.realize({20});
}

void scenario_shift_inwards_and_blend() {
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
    out.realize({20});
}

void scenario_renamed_outer() {
    Var x("x"), xo("xo"), xi("xi"), r("r"), ro("ro"), ri("ri");
    Func g("g"), f("f");
    g(x) = x;
    f(x) = g(x) + 1;
    g.compute_root();
    f.split(x, xo, xi, 8, TailStrategy::ShiftInwards)
        .rename(xo, r)
        .split(r, ro, ri, 2, TailStrategy::PredicateLoads);
    f.realize({20});
}

void scenario_fused_outer() {
    Var x("x"), y("y"), xo("xo"), xi("xi"), t("t"), to("to"), ti("ti");
    Func f("f"), out("out");
    f(x, y) = x + y;
    f(x, y) = f(x, y) * 3 + 1;
    out(x, y) = f(x, y);
    f.compute_root();
    // xo is the outer operand of the fuse, so the PredicateLoads overhang of t
    // runs xo past its loop max.
    f.update()
        .split(x, xo, xi, 8, TailStrategy::ShiftInwardsAndBlend)
        .fuse(y, xo, t)
        .split(t, to, ti, 5, TailStrategy::PredicateLoads);
    out.realize({20, 3});
}

// Accepted: xo is the *inner* operand of the fuse, so it stays in range.
bool accepted_fused_inner() {
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
    Buffer<int> r = out.realize({20, 3});
    for (int yy = 0; yy < 3; yy++) {
        for (int xx = 0; xx < 20; xx++) {
            if (r(xx, yy) != 3 * (xx + yy) + 1) {
                printf("[fused_inner] FAIL: out(%d, %d) = %d, expected %d\n",
                       xx, yy, r(xx, yy), 3 * (xx + yy) + 1);
                return false;
            }
        }
    }
    printf("[fused_inner] OK\n");
    return true;
}

// Accepted: a RoundUp parent does not shift its overhang back inwards.
bool accepted_round_up_parent() {
    Var x("x"), xo("xo"), xi("xi"), xoo("xoo"), xoi("xoi");
    Func g("g"), f("f"), out("out");
    g(x) = x;
    f(x) = g(x) + 1;
    out(x) = f(x);
    g.compute_root();
    f.compute_root()
        .split(x, xo, xi, 8, TailStrategy::RoundUp)
        .split(xo, xoo, xoi, 2, TailStrategy::PredicateLoads);
    Buffer<int> r = out.realize({20});
    for (int xx = 0; xx < 20; xx++) {
        if (r(xx) != xx + 1) {
            printf("[round_up_parent] FAIL: out(%d) = %d, expected %d\n", xx, r(xx), xx + 1);
            return false;
        }
    }
    printf("[round_up_parent] OK\n");
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    int failures = 0;
    failures += !expect_user_error("shift_inwards", reason, scenario_shift_inwards);
    failures += !expect_user_error("shift_inwards_and_blend", reason, scenario_shift_inwards_and_blend);
    failures += !expect_user_error("renamed_outer", reason, scenario_renamed_outer);
    failures += !expect_user_error("fused_outer", reason, scenario_fused_outer);
    failures += !accepted_fused_inner();
    failures += !accepted_round_up_parent();

    if (failures != 0) {
        printf("%d scenario(s) failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}

#else  // HALIDE_WITH_EXCEPTIONS

int main(int argc, char **argv) {
    printf("[SKIP] Halide was compiled without exceptions.\n");
    return 0;
}

#endif  // HALIDE_WITH_EXCEPTIONS

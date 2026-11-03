#include "Halide.h"
#include "expect_user_error.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
#ifndef HALIDE_WITH_EXCEPTIONS
    printf("[SKIP] the error cases need exceptions.\n");
    return 0;
#else
    int failures = 0;

    failures += !expect_user_error("untyped", "must be declared with an explicit type", []() {
        Func f("f"), g("g");
        Var x("x");
        f(x) = select(x < 1, 0, x + f(x - 1));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("2d_arbitrary", "not provably monotonically decreasing", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x < 2 || y < 2 || y > 6, 0, f(x - 1, y + 1)) + select(x < 2 || x > 6 || y < 2, 0, f(x + 1, y - 1));
        g(x, y) = f(x, y) * 2;
        g.realize({10, 10});
    });

    failures += !expect_user_error("cond_nested", "outside of a select", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < max(2, abs(select(x < 0, f(x - 1), 5))), 0, f(x - 1) + x);
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("cond_nested_select", "not provably monotonically decreasing", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 0, 0, f(x - 1) + select(x < f(x + 1), 0, 1));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("cond_self_reference", "outside of a select", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < f(x - 1), 0, f(x - 1) + x);
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("diagonal_fold", "fold factor", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x <= 0 || y <= 0, x + y, likely(f(x - 1, y - 1) + 1));
        g(x, y) = f(x, y);
        f.compute_at(g, x).store_root().fold_storage(y, 1);
        g.bound(x, 0, 64).bound(y, 0, 8);
        g.realize({64, 8});
    });

    failures += !expect_user_error("external_lag_fold", "fold factor", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));
        g(x, y) = f(x, y) + f(max(x - 1, 0), y);
        f.compute_at(g, x).store_root().fold_storage(x, 1);
        g.bound(x, 0, 64).bound(y, 0, 8);
        g.realize({64, 8});
    });

    failures += !expect_user_error("loop_1", "outside of a select", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 1, 0, f(x));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("loop_2", "inductive step is monotonically decreasing", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 2, 0, f(x - 1) + f(x) + f(x - 2));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("loop_3", "not provably monotonically decreasing", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 2, 0, f(x - 1) + f(min(2, x)) + f(x - 2));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("nested_select", "nested select", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 1, 0, select(x < 3, 1, f(x - 1)));
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("no_select", "outside of a select", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = x + f(x - 1);
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("redundant_z_fold", "fold factor", []() {
        Func f(Int(32), "f"), h("h");
        Var x("x"), y("y"), z("z");
        f(x, y) = select(x <= 0, y, likely(f(x - 1, y) + 1));
        h(x, y, z) = f(x, y);
        h.reorder(z, y, x);
        f.compute_at(h, z).store_root().fold_storage(x, 1);
        h.bound(x, 0, 64).bound(y, 0, 8).bound(z, 0, 4);
        h.realize({64, 8, 4});
    });

    failures += !expect_user_error("reorder", "same original inductive variable", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x"), xi("xi"), xo("xo");
        f(x) = select(x < 1, 0, x + f(x - 1));
        f.split(x, xo, xi, 8);
        f.reorder(xo, xi);
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("reorder_split", "same original inductive variable", []() {
        Func f(Float(32), 1, "f"), g("g");
        Var t("t"), to("to"), ti("ti");
        f(t) = select(t <= 0, 0.f, likely(f(t - 1) + 1.f));
        g(t) = f(t);
        f.compute_root().split(t, to, ti, 4).reorder(to, ti);
        g.realize({16});
    });

    failures += !expect_user_error("rvar_rhs", "not provably monotonically decreasing", []() {
        Func g(Int(32), "g"), h("h");
        Var x("x"), y("y");
        const int Y = 10;
        RDom ry(0, Y, "ry");
        g(x, y) = 0;
        g(x, y) = select(x <= 0, y, likely(max(g(x, y), g(x - 1, ry) + x + ry)));
        h(x, y) = g(x, y);
        h.bound(x, 0, 20).bound(y, 0, Y);
        g.compute_at(h, x).store_root();
        h.realize({20, Y});
    });

    failures += !expect_user_error("small_fold", "fold factor", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x <= 0, 0, f(x - 1) + f(x - 2) + x);
        g(x) = f(x) * 2;
        f.compute_at(g, x).store_root().fold_storage(x, 1);
        g.realize({10});
    });

    failures += !expect_user_error("tuple_fold", "fold factor", []() {
        Func f(std::vector<Type>{Int(32), Int(32)}, "f"), g("g");
        Var x("x");
        f(x) = select(x <= 0, Tuple(0, 0), Tuple(f(x - 1)[0] + 1, f(x - 1)[1] + f(x - 1)[0]));
        g(x) = f(x)[1] * 2;
        f.compute_at(g, x).store_root().fold_storage(x, 1);
        g.realize({10});
    });

    failures += !expect_user_error("tuple_to_expr", "returns a Tuple", []() {
        Func g(std::vector<Type>{Int(32), Int(32)}, "g"), h("h");
        Var x("x"), y("y");
        g(x, y) = Tuple(select(x <= 0, 0, g(max(0, x - 1), y) + x + y), select(y <= 0, 0, g(x, max(0, y - 1)) + x + y));
        h(x, y) = g(x, y)[0] / 2;
        h.realize({10, 10});
    });

    failures += !expect_user_error("two_defs", "after an inductive definition", []() {
        Func g(Int(32), "g"), h("h");
        Var x("x"), y("y");
        g(x, y) = select(x <= 0, y, likely(g(x - 1, y) + x));
        g(x, y) = select(y <= 0, g(x, y), likely(g(x, y - 1) + y));
        h(x, y) = g(x, y);
        h.bound(x, 0, 20).bound(y, 0, 20);
        g.compute_at(h, x).store_root();
        h.realize({20, 20});
    });

    failures += !expect_user_error("update", "after an inductive definition", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 1, 0, x + f(x - 1));
        f(x) += 1;
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("update_reorder", "nested inside all inductive", []() {
        Func g(Int(32), "g"), h("h");
        Var x("x"), y("y");
        const int Y = 10;
        RDom ry(0, Y, "ry");
        g(x, y) = 0;
        g(x, y) = select(x <= 0, y, likely(max(g(x, y), g(x - 1, ry) + x + ry)));
        g.update().reorder(x, ry);
        h(x, y) = g(x, y);
        h.bound(x, 0, 20).bound(y, 0, Y);
        g.compute_at(h, x).store_root();
        h.realize({20, Y});
    });

    failures += !expect_user_error("var_swap", "not provably monotonically decreasing", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x"), y("y");
        f(x, y) = select(x < 1, 0, x + f(y - 1, x - 1));
        g(x, y) = f(x, y) * 2;
        g.realize({10, 10});
    });

    failures += !expect_user_error("vectorize", "race condition", []() {
        Func f(Int(32), "f"), g("g");
        Var x("x");
        f(x) = select(x < 1, 0, x + f(x - 1));
        f.vectorize(x, 8);
        g(x) = f(x) * 2;
        g.realize({10});
    });

    failures += !expect_user_error("output", "inductively defined output", []() {
        Func f(Int(32), "f");
        Var x("x");
        f(x) = select(x < 1, 0, x + f(x - 1));
        f.realize({10});
    });

    if (failures) {
        printf("%d error cases failed\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
#endif
}

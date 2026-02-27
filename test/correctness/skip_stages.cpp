#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int call_count[4];
int call_counter(int x, int idx) {
    call_count[idx]++;
    return x;
}
HalideExtern_2(int, call_counter, int, int);

void reset_counts() {
    for (int i = 0; i < 4; i++) {
        call_count[i] = 0;
    }
}

void check_counts(int a = 0, int b = 0, int c = 0, int d = 0) {
    int correct[] = {a, b, c, d};
    for (int i = 0; i < 4; i++) {
        if (correct[i] != call_count[i]) {
            printf("call_count[%d] was supposed to be %d but instead is %d\n", i, correct[i], call_count[i]);
            exit(1);
        }
    }
}

int main(int argc, char **argv) {
    Var x, y;
    Param<bool> toggle1, toggle2;

    {
        // Make a diamond-shaped graph where only one of the two
        // side-lobes is used.
        Func f1, f2, f3, f4;
        f1(x) = x;
        f2(x) = call_counter(f1(x) + 1, 0);
        f3(x) = call_counter(f1(x) + 2, 1);
        f4(x) = select(toggle1, f2(x), f3(x));

        f1.compute_root();
        f2.compute_root();
        f3.compute_root();

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        f4.realize({10});
        check_counts(10, 0);

        reset_counts();
        toggle1.set(false);
        f4.realize({10});
        check_counts(0, 10);
    }

    {
        // Make a diamond-shaped graph where the first node can be
        // used in one of two ways.
        Func f1, f2, f3, f4;

        f1(x) = call_counter(x, 0);
        f2(x) = call_counter(f1(x) + 1, 1);
        f3(x) = call_counter(f1(x) + 1, 2);
        f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

        f1.compute_root();
        f2.compute_root();
        f3.compute_root();

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        toggle2.set(true);
        f4.realize({10});
        check_counts(10, 10, 10);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize({10});
        check_counts(10, 0, 10);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize({10});
        check_counts(10, 10, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize({10});
        check_counts(0, 0, 0);
    }

    {
        // Make a tuple-valued func where one value is used but the
        // other isn't. Currently we need to evaluate both, because we
        // have no way to turn only one of them off, and there might
        // be a recursive dependence of one on the other in an update
        // step.
        Func f1, f2;
        f1(x) = Tuple(call_counter(x, 0), call_counter(x + 1, 1));
        f2(x) = select(toggle1, f1(x)[0], 0) + f1(x)[1];
        f1.compute_root();

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);
        f2.realize({10});
        check_counts(10, 10);

        reset_counts();
        toggle1.set(false);
        f2.realize({10});
        check_counts(10, 10);
    }

    {
        // Make a tuple-valued func where neither value is used when
        // the toggle is false.
        Func f1, f2;
        f1(x) = Tuple(call_counter(x, 0), call_counter(x + 1, 1));
        f2(x) = select(toggle1, f1(x)[0], 0);
        f1.compute_root();
        f2.realize({10});

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);
        f2.realize({10});
        check_counts(10, 10);

        reset_counts();
        toggle1.set(false);
        f2.realize({10});
        check_counts(0, 0);
    }

    {
        // Make our two-toggle diamond-shaped graph again, but use a more complex schedule.
        Func f1, f2, f3, f4;

        f1(x) = call_counter(x, 0);
        f2(x) = call_counter(f1(x) + 1, 1);
        f3(x) = call_counter(f1(x) + 1, 2);
        f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

        Var xo, xi;
        f4.split(x, xo, xi, 5);
        f1.compute_at(f4, xo);
        f2.store_root().compute_at(f4, xo);
        f3.store_at(f4, xo).compute_at(f4, xi);

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        toggle2.set(true);
        f4.realize({10});
        check_counts(10, 10, 10);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize({10});
        check_counts(10, 0, 10);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize({10});
        check_counts(10, 10, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize({10});
        check_counts(0, 0, 0);
    }

    {
        // Test the interaction with sliding window. We don't need value of
        // g(5), but we need all values of f which is computed inside the g's
        // loop. Make sure we don't skip the computation of f.
        reset_counts();
        Func f("f"), g("g"), h("h");
        f(x) = call_counter(x, 0);
        g(x) = f(x) + f(x - 1);
        h(x) = select(x == 5, 0, g(x));

        f.store_root().compute_at(g, x);
        g.compute_at(h, x);
        h.realize({10});
        check_counts(11);
    }

    for (int test_case = 0; test_case <= 2; test_case++) {
        // Test a data-dependent stage skip. Double all values that exist in
        // rows that do not contain any negative numbers.
        Func input("input");
        input(x, y) = select(y % 3 == 0 && x == 37, -1, x);

        Func any_negative("any_negative");
        const int W = 100, H = 100;
        RDom r(0, W);
        any_negative(y) = cast<bool>(false);
        any_negative(y) = any_negative(y) || (input(r, y) < 0);

        Func doubled("doubled");
        doubled(x, y) = call_counter(input(x, y) * 2, 0);

        Func output("output");
        output(x, y) = select(any_negative(y), input(x, y), doubled(x, y));

        input.compute_root();

        if (test_case == 0) {
            // any_negative(y) is a constant condition over this loop, so 'double' can be skipped.
            doubled.compute_at(output, y);
            any_negative.compute_root();
        } else if (test_case == 1) {
            // any_negative(y) is not constant here, so 'double' can't be skipped.
            Var yo, yi;
            output.split(y, yo, yi, 10);
            doubled.compute_at(output, yo);
            any_negative.compute_root();
        } else {
            // double is computed outside of the consume node for any_negative,
            // so the condition can't be lifted because it contains a call that
            // may change in value.
            doubled.compute_at(output, y);
            any_negative.compute_at(output, y);
        }

        reset_counts();
        output.realize({W, H});
        check_counts(test_case == 0 ? 66 * 100 : 100 * 100);
    }

    {
        // Check the interation with storage hoisting

        // This Func may or may not be loaded, depending on y
        Func maybe_loaded("maybe_loaded");
        maybe_loaded(x, y) = x + y;

        // This Func may or may not be used, depending on y
        Func maybe_used("maybe_used");
        maybe_used(x, y) = maybe_loaded(x, y);

        Func output("output");
        output(x, y) = select(y % 100 == 37, 0, maybe_used(x, y));

        // The allocation condition depends on y, but the actual allocation
        // happens at the root level.
        maybe_loaded.compute_at(output, y).hoist_storage_root();
        maybe_used.compute_at(output, y).hoist_storage_root();

        // This will fail to compile with an undefined symbol if we haven't
        // handled the condition correctly.
        output.realize({100, 100});
    }

    printf("Success!\n");
    return 0;
}

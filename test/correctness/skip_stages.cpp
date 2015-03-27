#include "Halide.h"
#include <stdio.h>

using namespace Halide;



#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_count[4];
extern "C" DLLEXPORT int call_counter(int x, int idx) {
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
            exit(-1);
        }
    }
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::JavaScript)) {
        // TODO: Add JavaScript extern support.
        printf("Skipping skip stages test for JavaScript as it uses a C extern function.\n");
        return 0;
    }

    Var x;
    Param<bool> toggle1, toggle2;

    {
        // Make a diamond-shaped graph where only one of the two
        // side-lobes is used.
        Func f1, f2, f3, f4;
        f1(x) = x;
        f2(x) = call_counter(f1(x)+1, 0);
        f3(x) = call_counter(f1(x)+2, 1);
        f4(x) = select(toggle1, f2(x), f3(x));

        f1.compute_root();
        f2.compute_root();
        f3.compute_root();

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        f4.realize(10);
        check_counts(10, 0);

        reset_counts();
        toggle1.set(false);
        f4.realize(10);
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
        f4.realize(10);
        check_counts(10, 10, 10);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize(10);
        check_counts(10, 0, 10);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize(10);
        check_counts(10, 10, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize(10);
        check_counts(0, 0, 0);

    }

    {
        // Make a tuple-valued func where one value is used but the
        // other isn't. Currently we need to evaluate both, because we
        // have no way to turn only one of them off, and there might
        // be a recursive dependence of one on the other in an update
        // step.
        Func f1, f2;
        f1(x) = Tuple(call_counter(x, 0), call_counter(x+1, 1));
        f2(x) = select(toggle1, f1(x)[0], 0) + f1(x)[1];
        f1.compute_root();

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);
        f2.realize(10);
        check_counts(10, 10);

        reset_counts();
        toggle1.set(false);
        f2.realize(10);
        check_counts(10, 10);
    }

    {
        // Make a tuple-valued func where neither value is used when
        // the toggle is false.
        Func f1, f2;
        f1(x) = Tuple(call_counter(x, 0), call_counter(x+1, 1));
        f2(x) = select(toggle1, f1(x)[0], 0);
        f1.compute_root();
        f2.realize(10);

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);
        f2.realize(10);
        check_counts(10, 10);

        reset_counts();
        toggle1.set(false);
        f2.realize(10);
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
        f4.realize(10);
        check_counts(10, 10, 10);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize(10);
        check_counts(10, 0, 10);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize(10);
        check_counts(10, 10, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize(10);
        check_counts(0, 0, 0);
    }

    printf("Success!\n");
    return 0;
}

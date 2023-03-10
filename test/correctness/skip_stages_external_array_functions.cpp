#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int bounds_query_count[4];
int call_count[4];
extern "C" HALIDE_EXPORT_SYMBOL int call_counter(halide_buffer_t *input, int x, int idx, halide_buffer_t *output) {
    if (input->is_bounds_query()) {
        bounds_query_count[idx]++;
        input->dim[0] = output->dim[0];
        return 0;
    }
    call_count[idx]++;
    for (int32_t i = 0; i < output->dim[0].extent; i++) {
        output->host[i] = input->host[i] + x;
    }

    return 0;
}

void reset_counts() {
    for (int i = 0; i < 4; i++) {
        bounds_query_count[i] = 0;
        call_count[i] = 0;
    }
}

void check_queries(int a = 0, int b = 0, int c = 0, int d = 0) {
    int correct[] = {a, b, c, d};
    for (int i = 0; i < 4; i++) {
        if (correct[i] != bounds_query_count[i]) {
            printf("bounds_query_count[%d] was supposed to be %d but instead is %d\n", i, correct[i], bounds_query_count[i]);
            exit(1);
        }
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
    Var x;
    Param<bool> toggle1, toggle2;

    Buffer<uint8_t> out(10);

    {
        // Make a diamond-shaped graph where only one of the two
        // Side-lobes is used.
        Func f1, f2, f3, f4;
        f1(x) = cast<uint8_t>(x);
        f2.define_extern("call_counter", {f1, 1, 0}, UInt(8), 1);
        f3.define_extern("call_counter", {f1, 2, 1}, UInt(8), 1);
        f4(x) = select(toggle1, f2(x), f3(x));

        f1.compute_root();
        f2.compute_root();
        f3.compute_root();

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        f4.realize(out);
        for (int32_t i = 0; i < 10; i++) {
            assert(out(i) == i + 1);
        }
        check_queries(2, 2);
        check_counts(1, 0);

        reset_counts();
        toggle1.set(false);
        f4.realize(out);
        for (int32_t i = 0; i < 10; i++) {
            assert(out(i) == i + 2);
        }
        check_queries(2, 2);
        check_counts(0, 1);
    }

    {
        // Make a diamond-shaped graph where the first node can be
        // used in one of two ways.
        Func f1, f2, f3, f4;

        Func identity;
        identity(x) = x;

        f1.define_extern("call_counter",
                         {identity, 1, 0},
                         UInt(8), 1);
        Func f1_plus_one;
        f1_plus_one(x) = f1(x) + 1;

        f2.define_extern("call_counter",
                         {f1_plus_one, 1, 1},
                         UInt(8), 1);
        f3.define_extern("call_counter",
                         {f1_plus_one, 1, 2},
                         UInt(8), 1);
        f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

        identity.compute_root();
        f1_plus_one.compute_root();
        f1.compute_root();
        f2.compute_root();
        f3.compute_root();

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        toggle2.set(true);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 1, 1);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 0, 1);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 1, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(0, 0, 0);
    }

    {
        // Make a tuple-valued func where one value is used but the
        // other isn't. Currently we need to evaluate both, because we
        // have no way to turn only one of them off, and there might
        // be a recursive dependence of one on the other in an update
        // step.
        Func identity;
        identity(x) = x;

        Func extern1, extern2, f1, f2;
        extern1.define_extern("call_counter",
                              {identity, 0, 0},
                              UInt(8), 1);
        extern2.define_extern("call_counter",
                              {identity, 1, 1},
                              UInt(8), 1);

        f1(x) = Tuple(extern1(x), extern2(x + 1));
        f2(x) = select(toggle1, f1(x)[0], 0) + f1(x)[1];

        identity.compute_root();
        extern1.compute_root();
        extern2.compute_root();

        f1.compute_root();

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);

        f2.realize(out);
        check_queries(2, 2);
        check_counts(1, 1);

        reset_counts();
        toggle1.set(false);
        f2.realize(out);
        check_queries(2, 2);
        check_counts(1, 1);
    }

    {
        // Make a tuple-valued func where neither value is used when
        // the toggle is false.
        Func identity;
        identity(x) = x;

        Func extern1, extern2, f1, f2;
        extern1.define_extern("call_counter",
                              {identity, 0, 0},
                              UInt(8), 1);
        extern2.define_extern("call_counter",
                              {identity, 1, 1},
                              UInt(8), 1);

        f1(x) = Tuple(extern1(x), extern2(x + 1));
        f2(x) = select(toggle1, f1(x)[0], 0);

        identity.compute_root();
        extern1.compute_root();
        extern2.compute_root();

        f1.compute_root();
        f2.realize(out);

        f2.compile_jit();

        reset_counts();
        toggle1.set(true);
        f2.realize(out);
        check_queries(2, 2);
        check_counts(1, 1);

        reset_counts();
        toggle1.set(false);
        f2.realize(out);
        check_queries(2, 2);
        check_counts(0, 0);
    }

    {
        // Make our two-toggle diamond-shaped graph again, but use a more complex schedule.
        Func identity;
        identity(x) = x;

        Func extern1, extern2, extern3, f1, f2, f3, f4;
        extern1.define_extern("call_counter",
                              {identity, 0, 0},
                              UInt(8), 1);
        extern2.define_extern("call_counter",
                              {identity, 1, 1},
                              UInt(8), 1);
        extern3.define_extern("call_counter",
                              {identity, 1, 2},
                              UInt(8), 1);

        f1(x) = extern1(x);
        f2(x) = extern2(f1(x) + 1);
        f3(x) = extern3(f1(x) + 1);
        f4(x) = select(toggle1, f2(x), 0) + select(toggle2, f3(x), 0);

        identity.compute_root();
        extern1.compute_root();
        extern2.compute_root();
        extern3.compute_root();

        Var xo, xi;
        f4.split(x, xo, xi, 5);
        f1.compute_at(f4, xo);
        f2.store_root().compute_at(f4, xo);
        f3.store_at(f4, xo).compute_at(f4, xi);

        f4.compile_jit();

        reset_counts();
        toggle1.set(true);
        toggle2.set(true);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 1, 1);

        reset_counts();
        toggle1.set(false);
        toggle2.set(true);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 0, 1);

        reset_counts();
        toggle1.set(true);
        toggle2.set(false);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(1, 1, 0);

        reset_counts();
        toggle1.set(false);
        toggle2.set(false);
        f4.realize(out);
        check_queries(2, 2, 2);
        check_counts(0, 0, 0);
    }

    printf("Success!\n");
    return 0;
}

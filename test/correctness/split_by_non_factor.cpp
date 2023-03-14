#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    {
        // Check splitting an RVar in an update definition and then realizing it
        // over an extent that is not a multiple of the factor.
        Func f;
        f(x) = 0;
        f(x) += x;
        f.update().unroll(x, 2, TailStrategy::GuardWithIf);
        Buffer<int> result = f.realize({3});
        for (int i = 0; i < result.width(); i++) {
            if (result(i) != i) {
                printf("result(%d) was %d instead of %d\n", i, result(i), i);
                return 1;
            }
        }
    }

    {
        // Check splitting an update definition and a reduction domain
        Func f;

        f(x) = x;
        f(x) += 3;
        Param<int> sum_size;
        RDom r(0, sum_size);
        f(0) += f(r);

        f.update(0).vectorize(x, 8, TailStrategy::GuardWithIf);
        f.update(1).unroll(r, 4);

        // Just make sure that you can realize over any size
        // regardless of what the sum size is.
        for (int i = 1; i < 20; i++) {
            for (int j = 1; j < i; j++) {
                sum_size.set(j);
                f.realize({i});
            }
        }
    }

    {
        // Test something compute_at the inside and outside of a dimension split this way
        Func f, g, h;
        g(x) = x - 3;
        h(x) = x * 7;
        f(x) = 0;
        f(x) += g(x) + h(x);
        Var xo, xi;
        f.update().split(x, xo, xi, 7, TailStrategy::GuardWithIf);
        g.compute_at(f, xo);
        h.compute_at(f, xi);
        Buffer<int> result = f.realize({15});
        for (int i = 0; i < result.width(); i++) {
            int correct = (i - 3) + i * 7;
            int actual = result(i);
            if (actual != correct) {
                printf("result(%d) = %d instead of %d\n", i, actual, correct);
                return 1;
            }
        }
    }

    {
        // Test splitting the inner dimension of a pure var in an update
        Func f;
        f(x) = x;
        f(x) += 1;
        Var xo, xi, xio, xii;
        f.compute_root();
        f.update().split(x, xo, xi, 4).split(xi, xio, xii, 6);
        Func g;
        g(x) = f(x);
        Buffer<int> result = g.realize({32});
        for (int i = 0; i < result.width(); i++) {
            int correct = i + 1;
            int actual = result(i);
            if (actual != correct) {
                printf("result(%d) = %d instead of %d\n", i, actual, correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

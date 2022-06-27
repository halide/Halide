#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL int count(int x) {
    return call_counter++;
}
HalideExtern_1(int, count, int);

int main(int argc, char **argv) {
    {
        // Split an rvar, compute something at the inner var, and make
        // sure the it's evaluated the right number of times.
        Func f, g;
        Var x;
        RDom r(0, 10);
        RVar ri, ro;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 2);
        f.compute_at(g, ri);

        Buffer<int> im = g.realize({10});

        if (call_counter != 10) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            if (im(i) != i) {
                printf("im(%d) = %d instead of %d\n", i, im(i), i);
                return -1;
            }
        }
        call_counter = 0;
    }

    {
        // Now at the outer var.
        Func f, g;
        Var x;
        RDom r(0, 10);
        RVar ri, ro;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 2);
        f.compute_at(g, ro).unroll(x);

        Buffer<int> im = g.realize({10});

        if (call_counter != 10) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            if (im(i) != i) {
                printf("im(%d) = %d instead of %d\n", i, im(i), i);
                return -1;
            }
        }
        call_counter = 0;
    }

    {
        // Now at the inner var with it unrolled.
        Func f, g;
        Var x;
        RDom r(0, 10);
        RVar ri, ro;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 2).unroll(ri);
        f.compute_at(g, ri);

        Buffer<int> im = g.realize({10});

        if (call_counter != 10) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            if (im(i) != i) {
                printf("im(%d) = %d instead of %d\n", i, im(i), i);
                return -1;
            }
        }
        call_counter = 0;
    }

    {
        // Now reorder and compute at the new inner. They'll come out
        // in a different order this time, but there should still be
        // 10 evaluations of f.
        Func f, g;
        Var x;
        RDom r(0, 10);
        RVar ri, ro;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 2).reorder(ro, ri);
        f.compute_at(g, ro);

        Buffer<int> im = g.realize({10});

        if (call_counter != 10) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            int correct = (i / 2) + ((i % 2 == 0) ? 0 : 5);
            if (im(i) != correct) {
                printf("im(%d) = %d instead of %d\n", i, im(i), correct);
                return -1;
            }
        }
        call_counter = 0;
    }

    {
        // Now split twice and fuse the outer two vars and compute within that.
        Func f, g;
        Var x;
        RDom r(0, 20);
        RVar rio, rii, ri, ro, fused;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 4).split(ri, rio, rii, 2).fuse(rio, ro, fused);
        f.compute_at(g, fused);

        Buffer<int> im = g.realize({20});

        if (call_counter != 20) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            int correct = i;
            if (im(i) != correct) {
                printf("im(%d) = %d instead of %d\n", i, im(i), correct);
                return -1;
            }
        }
        call_counter = 0;
    }

    {
        // Split by a non-factor and compute something at the split,
        // to check if guardwithif is respected in the bounds
        // relationship.
        Func f, g;
        Var x;
        RDom r(0, 10);
        RVar ri, ro;
        f(x) = count(x);
        g(x) = 0;
        g(r) = f(r);

        g.update().split(r, ro, ri, 3);
        f.compute_at(g, ro);

        Buffer<int> im = g.realize({10});

        if (call_counter != 10) {
            printf("Wrong number of calls to f: %d\n", call_counter);
            return -1;
        }

        for (int i = 0; i < im.width(); i++) {
            if (im(i) != i) {
                printf("im(%d) = %d instead of %d\n", i, im(i), i);
                return -1;
            }
        }
        call_counter = 0;
    }

    printf("Success!\n");
    return 0;
}

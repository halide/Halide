#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    {
        // Define a reduction with two update steps
        Func f;

        f(x) = sin(x);

        RDom r1(1, 10);
        Expr xl = r1;       // left to right pass
        Expr xr = 10 - r1;  // right to left pass
        f(xl) = f(xl - 1) + f(xl);
        f(xr) = f(xr + 1) + f(xr);

        Buffer<float> result = f.realize({11});

        // The same thing in C
        float ref[11];
        for (int i = 0; i < 11; i++) {
            ref[i] = sinf(i);
        }
        for (int i = 1; i < 11; i++) {
            ref[i] += ref[i - 1];
        }
        for (int i = 9; i >= 0; i--) {
            ref[i] += ref[i + 1];
        }

        for (int i = 0; i < 11; i++) {
            if (fabs(result(i) - ref[i]) > 0.0001f) {
                printf("result(%d) = %f instead of %f\n",
                       i, result(i), ref[i]);
                return 1;
            }
        }
    }

    {
        // Define a reduction that fills an array, integrates it, then
        // manually change certain values. One of the values will
        // depend on another function.
        Func f, g;
        g(x) = x * x;
        f(x) = x;

        // Integrate from 1 to 10
        RDom r(1, 10);
        f(r) = f(r) + f(r - 1);

        // Clobber two values
        f(17) = 8;
        f(109) = 4;

        // Clobber a range using another func
        RDom r2(4, 5);
        f(r2) = g(r2);

        g.compute_at(f, r2);
        Buffer<int> result = f.realize({110});

        int correct[110];
        for (int i = 0; i < 110; i++) {
            correct[i] = i;
        }
        for (int i = 1; i < 11; i++) {
            correct[i] += correct[i - 1];
        }
        correct[17] = 8;
        correct[109] = 4;
        for (int i = 4; i < 9; i++) {
            correct[i] = i * i;
        }

        for (int i = 0; i < 110; i++) {
            if (correct[i] != result(i)) {
                printf("result(%d) = %d instead of %d\n",
                       i, result(i), correct[i]);
                return 1;
            }
        }
    }

    {
        // Create a fully unrolled fibonacci routine composed almost
        // entirely of single assignment statements. The horror!
        Func f;
        f(x) = 1;
        for (int i = 2; i < 20; i++) {
            f(i) = f(i - 1) + f(i - 2);
        }

        Buffer<int> result = f.realize({20});

        int ref[20];
        ref[0] = 1;
        ref[1] = 1;
        for (int i = 2; i < 20; i++) {
            ref[i] = ref[i - 1] + ref[i - 2];
            if (ref[i] != result(i)) {
                printf("fibonacci(%d) = %d instead of %d\n",
                       i, result(i), ref[i]);
                return 1;
            }
        }
    }

    {
        // Make an integral image
        Func f;
        f(x, y) = sin(x + y);

        RDom r(1, 99);
        f(x, r) += f(x, r - 1);
        f(r, y) += f(r - 1, y);

        // Walk down the image in vectors
        f.update(0).vectorize(x, 4);

        // Walk across the image in parallel. We need to do an unsafe
        // reorder operation here to move y to the outer loop, because
        // we don't have the ability to reorder vars with rvars yet.
        f.update(1).reorder(Var(r.x.name()), y).parallel(y);

        Buffer<float> result = f.realize({100, 100});

        // Now the equivalent in C (cheating and using Halide for the initial image)
        Buffer<float> ref = lambda(x, y, sin(x + y)).realize({100, 100});
        for (int y = 1; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                ref(x, y) += ref(x, y - 1);
            }
        }
        for (int y = 0; y < 100; y++) {
            for (int x = 1; x < 100; x++) {
                ref(x, y) += ref(x - 1, y);
            }
        }

        // Check they're the same
        for (int y = 0; y < 100; y++) {
            for (int x = 0; x < 100; x++) {
                if (fabs(ref(x, y) - result(x, y)) > 0.0001f) {
                    printf("integral image at (%d, %d) = %f instead of %f\n",
                           x, y, result(x, y), ref(x, y));
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");

    return 0;
}

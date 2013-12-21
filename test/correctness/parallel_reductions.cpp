#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {

    {
        RDom r1(0, 16), r2(0, 16);
        Var i, j;
        Func f1, f2;

        // Sum of the first 256 integers, vectorized and parallelized
        // Somewhat clunky syntax for now, because we're going to do a
        // hierarchical decomposition of the reduction, which we can't
        // do with the schedule yet.

        // The first stage sums runs of 16 elements. Each run will be done in parallel and vectorized.
        f1(i) = sum(i*16 + r1);

        // The second stage does the final gather over the runs
        f2() = sum(f1(r2));

        // Vectorize by a factor of four, then parallelize the rest
        f1.compute_root().vectorize(i, 4).parallel(i);

        Image<int> im = f2.realize();

        int correct = (256*255)/2;
        if (im(0) != correct) {
            printf("im(0) = %d instead of %d\n", im(0), correct);
            return -1;
        }
    }


    {

        RDom r1(1, 15);
        Var i, j;
        Func f1, f2;

        // Now we'll try a parallelized and vectorized prefix sum
        Image<int> input(256), correct(256);
        for (int i = 0; i < 256; i++) {
            input(i) = rand() % 16;
            correct(i) = input(i);
            if (i > 0) correct(i) += correct(i-1);
        }

        // We lay the input out row-major in a 2D array
        f1(i, j) = input(i + j*16);

        // Sum along the rows
        f1(r1, j) += f1(r1-1, j);

        // Sum down the last column
        f1(15, r1) += f(15, r1-1);

        // Fix the first column
        f1(0, r1) += f(15, r1-1);

        // Correct the rest of each row
        RDom r2(1, 14);
        f1(r2, j) += f(r2-1, j);

        // Then each output is a combination of two terms.
        Func out;
        Expr x = i%16, y = i/16;
        out(i) = f1(x, y);

        f1.compute_root().vectorize(i, 4).parallel(i);
        f1.update(0).vectorize(i, 4).parallel(i);
        f1.update(3).parallel(j);

        out.vectorize(i, 4).parallel(i);

        Image<int> result = out.realize(256);

        for (int i = 0; i < 256; i++) {
            if (result(i) != correct(i)) {
                printf("result(%d) = %d instead of %d\n", i, result(i), correct(i));
                return -1;
            }
        }

    }

    printf("Success!\n");
    return 0;
}



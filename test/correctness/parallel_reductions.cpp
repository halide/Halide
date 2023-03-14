#include "Halide.h"
#include <stdio.h>

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
        f1(i) = sum(i * 16 + r1);

        // The second stage does the final gather over the runs
        f2() = sum(f1(r2));

        // Vectorize by a factor of four, then parallelize the rest
        f1.compute_root().vectorize(i, 4).parallel(i);

        Buffer<int> im = f2.realize();

        int correct = (256 * 255) / 2;
        if (im(0) != correct) {
            printf("im(0) = %d instead of %d\n", im(0), correct);
            return 1;
        }
    }

    {
        Var i, j;

        // Now we'll try a parallelized and vectorized prefix sum
        Buffer<int> input(256), correct(256);
        for (int i = 0; i < 256; i++) {
            input(i) = rand() % 16;
            correct(i) = input(i);
            if (i > 0) correct(i) += correct(i - 1);
        }

        int chunk_size = 16;

        RDom r1(0, chunk_size);

        // Lay out the input in 2D, and do a sum scan of each row
        Func sum_rows;
        sum_rows(i, j) = 0;
        sum_rows(r1, j) = sum_rows(r1 - 1, j) + input(r1 + j * chunk_size);

        // Sum down the last column to compute the sum of the previous n rows.
        Func sum_cols;
        sum_cols(j) = 0;
        sum_cols(r1) += sum_cols(r1 - 1) + sum_rows(chunk_size - 1, r1);

        // Then each output is a column sum plus a row sum.
        Func out;
        Expr x = i % chunk_size, y = i / chunk_size;
        out(i) = sum_rows(x, y) + sum_cols(y - 1);

        Var ii, io;
        out.split(i, io, ii, chunk_size).vectorize(ii, 4).parallel(io);
        sum_rows.compute_root().vectorize(i, 4).parallel(j);
        sum_rows.update().parallel(j);
        sum_cols.compute_root().vectorize(j, 4);
        sum_cols.update().unscheduled();
        out.output_buffer().dim(0).set_bounds(0, 256);

        Buffer<int> result = out.realize({256});

        for (int i = 0; i < 256; i++) {
            if (result(i) != correct(i)) {
                printf("result(%d) = %d instead of %d\n", i, result(i), correct(i));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

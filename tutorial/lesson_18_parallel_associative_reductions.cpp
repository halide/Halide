// Halide tutorial lesson 18: Factoring an associative reduction using rfactor

// This lesson demonstrates how to parallelize or vectorize an associative
// reduction using the scheduling directive 'rfactor'.

// On linux, you can compile and run it like so:
// g++ lesson_18*.cpp -g -I ../include -L ../bin -lHalide -lpthread -ldl -o lesson_18 -std=c++11
// LD_LIBRARY_PATH=../bin ./lesson_18

// On os x:
// g++ lesson_18*.cpp -g -I ../include -L ../bin -lHalide -o lesson_18 -std=c++11
// DYLD_LIBRARY_PATH=../bin ./lesson_18

// If you have the entire Halide source tree, you can also build it by
// running:
//    make tutorial_lesson_18_parallel_associative_reductions
// in a shell with the current directory at the top of the halide
// source tree.

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Declare some Vars to use below.
    Var x("x"), y("y"), i("i"), u("u"), v("v");

    // Create an input with random values.
    Buffer<uint8_t> input(8, 8, "input");
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            input(x, y) = (rand() % 256);
        }
    }

    {
        // As mentioned previously in lesson 9, parallelizing variables that
        // are part of a reduction domain is tricky, since there may be data
        // dependencies across those variables.

        // Consider the histogram example in lesson 9:
        Func histogram("hist_serial");
        histogram(i) = 0;
        RDom r(0, input.width(), 0, input.height());
        histogram(input(r.x, r.y) / 32) += 1;

        histogram.vectorize(i, 8);
        histogram.realize(8);

        // See figures/lesson_18_hist_serial.mp4 for a visualization of
        // what this does.

        // We can vectorize the initialization of the histogram
        // buckets, but since there are data dependencies across r.x
        // and r.y in the update definition (i.e. the update refers to
        // value computed in the previous iteration), we can't
        // parallelize or vectorize r.x or r.y without introducing a
        // race condition. The following code would produce an error:
        // histogram.update().parallel(r.y);
    }

    {
        // Note, however, that the histogram operation (which is a
        // kind of sum reduction) is associative. A common trick to
        // speed-up associative reductions is to slice up the
        // reduction domain into smaller slices, compute a partial
        // result over each slice, and then merge the results. Since
        // the computation of each slice is independent, we can
        // parallelize over slices.

        // Going back to the histogram example, we slice the reduction
        // domain into rows by defining an intermediate function that
        // computes the histogram of each row independently:
        Func intermediate("intm_par_manual");
        intermediate(i, y) = 0;
        RDom rx(0, input.width());
        intermediate(input(rx, y) / 32, y) += 1;

        // We then define a second stage which sums those partial
        // results:
        Func histogram("merge_par_manual");
        histogram(i) = 0;
        RDom ry(0, input.height());
        histogram(i) += intermediate(i, ry);

        // Since the intermediate no longer has data dependencies
        // across the y dimension, we can parallelize it over y:
        intermediate.compute_root().update().parallel(y);

        // We can also vectorize the initializations.
        intermediate.vectorize(i, 8);
        histogram.vectorize(i, 8);

        histogram.realize(8);

        // See figures/lesson_18_hist_manual_par.mp4 for a visualization of
        // what this does.
    }

    {
        // This manual factorization of an associative reduction can
        // be tedious and bug-prone. Although it's fairly easy to do
        // manually for the histogram, it can get complex pretty fast,
        // especially if the RDom may has a predicate (RDom::where),
        // or when the function reduces onto a multi-dimensional
        // tuple.

        // Halide provides a way to do this type of factorization
        // through the scheduling directive 'rfactor'. rfactor splits
        // an associative update definition into an intermediate which
        // computes the partial results over slices of a reduction
        // domain and replaces the current update definition with a
        // new definition which merges those partial results.

        // Using rfactor, we don't need to change the algorithm at all:
        Func histogram("hist_rfactor_par");
        histogram(x) = 0;
        RDom r(0, input.width(), 0, input.height());
        histogram(input(r.x, r.y) / 32) += 1;

        // The task of factoring of associative reduction is moved
        // into the schedule, via rfactor. rfactor takes as input a
        // list of <RVar, Var> pairs, which contains list of reduction
        // variables (RVars) to be made "parallelizable". In the
        // generated intermediate Func, all references to this
        // reduction variables are replaced with references to "pure"
        // variables (the Vars). Since, by construction, Vars are
        // race-condition free, the intermediate reduction is now
        // parallelizable across those dimensions. All reduction
        // variables not in the list are removed from the original
        // function and "lifted" to the intermediate.

        // To generate the same code as the manually-factored version,
        // we do the following:
        Func intermediate = histogram.update().rfactor({{r.y, y}});
        // We pass {r.y, y} as the argument to rfactor to make the
        // histogram parallelizable across the y dimension, similar to
        // the manually-factored version.
        intermediate.compute_root().update().parallel(y);

        // In the case where you are only slicing up the domain across
        // a single variable, you can actually drop the braces and
        // write the rfactor the following way.
        // Func intermediate = histogram.update().rfactor(r.y, y);

        // Vectorize the initializations, as we did above.
        intermediate.vectorize(x, 8);
        histogram.vectorize(x, 8);

        // It is important to note that rfactor (or reduction
        // factorization in general) only works for associative
        // reductions. Associative reductions have the nice property
        // that their results are the same no matter how the
        // computation is grouped (i.e. split into chunks). If rfactor
        // can't prove the associativity of a reduction, it will throw
        // an error.

        Buffer<int> halide_result = histogram.realize(8);

        // See figures/lesson_18_hist_rfactor_par.mp4 for a
        // visualization of what this does.

        // The equivalent C is:
        int c_intm[8][8];
        for (int y = 0; y < input.height(); y++) {
            for (int x = 0; x < 8; x++) {
                c_intm[y][x] = 0;
            }
        }
        /* parallel */ for (int y = 0; y < input.height(); y++) {
            for (int r_x = 0; r_x < input.width(); r_x++) {
                c_intm[y][input(r_x, y) / 32] += 1;
            }
        }

        int c_result[8];
        for (int x = 0; x < 8; x++) {
            c_result[x] = 0;
        }
        for (int x = 0; x < 8; x++) {
            for (int r_y = 0; r_y < input.height(); r_y++) {
                c_result[x] += c_intm[r_y][x];
            }
        }

        // Check the answers agree:
        for (int x = 0; x < 8; x++) {
            if (c_result[x] != halide_result(x)) {
                printf("halide_result(%d) = %d instead of %d\n",
                       x, halide_result(x), c_result[x]);
                return -1;
            }
        }
    }

    {
        // Now that we can factor associative reductions with the
        // scheduling directive 'rfactor', we can explore various
        // factorization strategies using the schedule alone. Given
        // the same serial histogram code:
        Func histogram("hist_rfactor_vec");
        histogram(x) = 0;
        RDom r(0, input.width(), 0, input.height());
        histogram(input(r.x, r.y) / 32) += 1;

        // Instead of r.y, we rfactor on r.x this time to slice the
        // domain into columns.
        Func intermediate = histogram.update().rfactor(r.x, u);

        // Now that we're computing an independent histogram
        // per-column, we can vectorize over columns.
        intermediate.compute_root().update().vectorize(u, 8);

        // Note that since vectorizing the inner dimension changes the
        // order in which values are added to the final histogram
        // buckets computations, so this trick only works if the
        // associative reduction is associative *and*
        // commutative. rfactor will attempt to prove these properties
        // hold and will throw an error if it can't.

        // Vectorize the initializations.
        intermediate.vectorize(x, 8);
        histogram.vectorize(x, 8);

        Buffer<int> halide_result = histogram.realize(8);

        // See figures/lesson_18_hist_rfactor_vec.mp4 for a
        // visualization of what this does.

        // The equivalent C is:
        int c_intm[8][8];
        for (int u = 0; u < input.width(); u++) {
            for (int x = 0; x < 8; x++) {
                c_intm[u][x] = 0;
            }
        }
        for (int r_y = 0; r_y < input.height(); r_y++) {
            for (int u = 0; u < input.width() / 8; u++) {
                /* vectorize */ for (int u_i = 0; u_i < 8; u_i++) {
                    c_intm[u*4 + u_i][input(u*8 + u_i, r_y) / 32] += 1;
                }
            }
        }

        int c_result[8];
        for (int x = 0; x < 8; x++) {
            c_result[x] = 0;
        }
        for (int x = 0; x < 8; x++) {
            for (int r_x = 0; r_x < input.width(); r_x++) {
                c_result[x] += c_intm[r_x][x];
            }
        }

        // Check the answers agree:
        for (int x = 0; x < 8; x++) {
            if (c_result[x] != halide_result(x)) {
                printf("halide_result(%d) = %d instead of %d\n",
                       x, halide_result(x), c_result[x]);
                return -1;
            }
        }
    }

    {
        // We can also slice a reduction domain up over multiple
        // dimensions at once. This time, we'll compute partial
        // histograms over tiles of the domain.
        Func histogram("hist_rfactor_tile");
        histogram(x) = 0;
        RDom r(0, input.width(), 0, input.height());
        histogram(input(r.x, r.y) / 32) += 1;

        // We first split both r.x and r.y by a factor of four.
        RVar rx_outer("rx_outer"), rx_inner("rx_inner");
        RVar ry_outer("ry_outer"), ry_inner("ry_inner");
        histogram.update()
            .split(r.x, rx_outer, rx_inner, 4)
            .split(r.y, ry_outer, ry_inner, 4);

        // We now call rfactor to make an intermediate function that
        // independently computes a histogram of each tile.
        Func intermediate = histogram.update().rfactor({{rx_outer, u}, {ry_outer, v}});

        // We can now parallelize the intermediate over tiles.
        intermediate.compute_root().update().parallel(u).parallel(v);

        // We also reorder the tile indices outermost to give the
        // classic tiled traversal.
        intermediate.update().reorder(rx_inner, ry_inner, u, v);

        // Vectorize the initializations.
        intermediate.vectorize(x, 8);
        histogram.vectorize(x, 8);

        Buffer<int> halide_result = histogram.realize(8);

        // See figures/lesson_18_hist_rfactor_tile.mp4 for a visualization of
        // what this does.

        // The equivalent C is:
        int c_intm[4][4][8];
        for (int v = 0; v < input.height() / 2; v++) {
            for (int u = 0; u < input.width() / 2; u++) {
                for (int x = 0; x < 8; x++) {
                    c_intm[v][u][x] = 0;
                }
            }
        }
        /* parallel */ for (int v = 0; v < input.height() / 2; v++) {
            /* parallel */ for (int u = 0; u < input.width() / 2; u++) {
                for (int ry_inner = 0; ry_inner < 2; ry_inner++) {
                    for (int rx_inner = 0; rx_inner < 2; rx_inner++) {
                        c_intm[v][u][input(u*2 + rx_inner, v*2 + ry_inner) / 32] += 1;
                    }
                }
            }
        }

        int c_result[8];
        for (int x = 0; x < 8; x++) {
            c_result[x] = 0;
        }
        for (int x = 0; x < 8; x++) {
            for (int ry_outer = 0; ry_outer < input.height() / 2; ry_outer++) {
                for (int rx_outer = 0; rx_outer < input.width() / 2; rx_outer++) {
                    c_result[x] += c_intm[ry_outer][rx_outer][x];
                }
            }
        }

        // Check the answers agree:
        for (int x = 0; x < 8; x++) {
            if (c_result[x] != halide_result(x)) {
                printf("halide_result(%d) = %d instead of %d\n",
                       x, halide_result(x), c_result[x]);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;
}

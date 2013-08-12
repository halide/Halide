#include <Halide.h>
#include <stdio.h>
#include <algorithm>
#include "clock.h"

using namespace Halide;

Var x("x"), y("y");

Func bitonic_sort(Func input, int size) {
    Func next, prev = input;

    Var xo("xo"), xi("xi");

    for (int pass_size = 1; pass_size < size; pass_size <<= 1) {
        for (int chunk_size = pass_size; chunk_size > 0; chunk_size >>= 1) {
            next = Func("bitonic_pass");
            Expr chunk_start = (x/(2*chunk_size))*(2*chunk_size);
            Expr chunk_end = (x/(2*chunk_size) + 1)*(2*chunk_size);
            Expr chunk_middle = chunk_start + chunk_size;
            Expr chunk_index = x - chunk_start;
            if (pass_size == chunk_size && pass_size > 1) {
                // Flipped pass
                Expr partner = 2*chunk_middle - x - 1;
                // We need a clamp here to help out bounds inference
                partner = clamp(partner, chunk_start, chunk_end-1);
                next(x) = select(x < chunk_middle,
                                 min(prev(x), prev(partner)),
                                 max(prev(x), prev(partner)));


            } else {
                // Regular pass
                Expr partner = chunk_start + (chunk_index + chunk_size) % (chunk_size*2);
                next(x) = select(x < chunk_middle,
                                 min(prev(x), prev(partner)),
                                 max(prev(x), prev(partner)));


            }

            if (pass_size > 1) {
                next.split(x, xo, xi, 2*chunk_size);
            }
            if (chunk_size > 128) {
                next.parallel(xo);
            }
            next.compute_root();
            prev = next;
        }
    }

    return next;
}

// Merge sort contiguous chunks of size s in a 1d func.
Func merge_sort(Func input, int total_size, int chunk_size) {
    Var xi("xi"), xo("xo");

    if (chunk_size == 1) {
        return input;
    }

    if (chunk_size == 4) {
        // Use a small sorting network
        Func result;
        Expr x_base = (x/4)*4;
        Expr a0 = input(x_base);
        Expr a1 = input(x_base+1);
        Expr a2 = input(x_base+2);
        Expr a3 = input(x_base+3);

        Expr b0 = min(a0, a1);
        Expr b1 = max(a0, a1);
        Expr b2 = min(a2, a3);
        Expr b3 = max(a2, a3);

        a0 = min(b0, b3);
        a1 = min(b1, b2);
        a2 = max(b1, b2);
        a3 = max(b0, b3);

        b0 = min(a0, a1);
        b1 = max(a0, a1);
        b2 = min(a2, a3);
        b3 = max(a2, a3);

        result(x) = select(x % 4 == 0, b0,
                           select(x % 4 == 1, b1,
                                  select(x % 4 == 2, b2, b3)));

        result.split(x, xo, xi, 4).unroll(xi);
        result.bound(x, 0, total_size);
        return result;
    }

    // Sort the halves
    Func recur = merge_sort(input, total_size, chunk_size / 2);

    // Merge within each chunk
    Func merge_rows("merge_rows");
    RDom r(0, chunk_size);

    // The first dimension of merge_rows is within the chunk, and the
    // second dimension is the chunk index.  Keeps track of two
    // pointers we're merging from and an output value.
    merge_rows(x, y) = Tuple(0, 0, cast(input.value().type(), 0));

    Expr candidate_a = merge_rows(r-1, y)[0];
    Expr candidate_b = merge_rows(r-1, y)[1];
    Expr valid_a = candidate_a < chunk_size/2;
    Expr valid_b = candidate_b < chunk_size/2;
    Expr value_a = recur(y*chunk_size + clamp(candidate_a, 0, chunk_size/2-1));
    Expr value_b = recur(y*chunk_size + chunk_size/2 + clamp(candidate_b, 0, chunk_size/2-1));
    merge_rows(r, y) = tuple_select(valid_a && ((value_a < value_b) || !valid_b),
                                    Tuple(candidate_a + 1, candidate_b, value_a),
                                    Tuple(candidate_a, candidate_b + 1, value_b));


    Func result("result");
    result(x) = merge_rows(x%chunk_size, x/chunk_size)[2];

    result.split(x, xo, xi, chunk_size);
    recur.compute_root();
    merge_rows.compute_at(result, xo);
    if (chunk_size > 16) {
        result.parallel(xo);
    }

    return result;
}

int main(int argc, char **argv) {

    const int N = 1 << 15;

    Image<int> data(N);
    for (int i = 0; i < N; i++) {
        data(i) = rand() & 0xfffff;
    }
    Func input = lambda(x, data(x));

    printf("Bitonic sort...\n");
    Func f = bitonic_sort(input, N);
    f.bound(x, 0, N);
    f.compile_jit();
    Image<int> bitonic_sorted(N);
    double t1 = currentTime();
    f.realize(bitonic_sorted);
    double t2 = currentTime();

    printf("Merge sort...\n");
    f = merge_sort(input, N, N);
    f.bound(x, 0, N);
    f.compile_jit();
    Image<int> merge_sorted(N);
    double t3 = currentTime();
    f.realize(merge_sorted);
    double t4 = currentTime();

    Image<int> correct(N);
    for (int i = 0; i < N; i++) {
        correct(i) = data(i);
    }
    printf("std::sort...\n");
    double t5 = currentTime();
    std::sort(&correct(0), &correct(N));
    double t6 = currentTime();

    printf("Times:\n"
           "bitonic sort: %f \n"
           "merge sort: %f \n"
           "std::sort %f\n",
           t2-t1, t4-t3, t6-t5);

    if (N <= 100) {
        for (int i = 0; i < N; i++) {
            printf("%8d %8d %8d\n",
                   correct(i), bitonic_sorted(i), merge_sorted(i));
        }
    }

    for (int i = 0; i < N; i++) {
        if (bitonic_sorted(i) != correct(i)) {
            printf("bitonic sort failed: %d -> %d instead of %d\n", i, bitonic_sorted(i), correct(i));
            return -1;
        }
        if (merge_sorted(i) != correct(i)) {
            printf("merge sort failed: %d -> %d instead of %d\n", i, merge_sorted(i), correct(i));
            return -1;
        }
    }

    return 0;
}

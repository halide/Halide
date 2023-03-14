#include "Halide.h"
#include "halide_benchmark.h"
#include <algorithm>
#include <cstdio>

using namespace Halide;
using namespace Halide::Tools;

Var x("x"), y("y");

Func bitonic_sort(Func input, int size) {
    Func next, prev = input;

    Var xo("xo"), xi("xi");

    for (int pass_size = 1; pass_size < size; pass_size <<= 1) {
        for (int chunk_size = pass_size; chunk_size > 0; chunk_size >>= 1) {
            next = Func("bitonic_pass");
            Expr chunk_start = (x / (2 * chunk_size)) * (2 * chunk_size);
            Expr chunk_end = (x / (2 * chunk_size) + 1) * (2 * chunk_size);
            Expr chunk_middle = chunk_start + chunk_size;
            Expr chunk_index = x - chunk_start;
            if (pass_size == chunk_size && pass_size > 1) {
                // Flipped pass
                Expr partner = 2 * chunk_middle - x - 1;
                // We need a clamp here to help out bounds inference
                partner = clamp(partner, chunk_start, chunk_end - 1);
                next(x) = select(x < chunk_middle,
                                 min(prev(x), prev(partner)),
                                 max(prev(x), prev(partner)));

            } else {
                // Regular pass
                Expr partner = chunk_start + (chunk_index + chunk_size) % (chunk_size * 2);
                next(x) = select(x < chunk_middle,
                                 min(prev(x), prev(partner)),
                                 max(prev(x), prev(partner)));
            }

            if (pass_size > 1) {
                next.split(x, xo, xi, 2 * chunk_size);
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
Func merge_sort(Func input, int total_size) {
    std::vector<Func> stages;
    Func result;

    const int parallel_work_size = 512;

    Func parallel_stage("parallel_stage");

    // First gather the input into a 2D array of width four where each row is sorted
    {
        assert(input.dimensions() == 1);
        // Use a small sorting network
        Expr a0 = input(4 * y);
        Expr a1 = input(4 * y + 1);
        Expr a2 = input(4 * y + 2);
        Expr a3 = input(4 * y + 3);

        Expr b0 = min(a0, a1);
        Expr b1 = max(a0, a1);
        Expr b2 = min(a2, a3);
        Expr b3 = max(a2, a3);

        a0 = min(b0, b2);
        a1 = max(b0, b2);
        a2 = min(b1, b3);
        a3 = max(b1, b3);

        b0 = a0;
        b1 = min(a1, a2);
        b2 = max(a1, a2);
        b3 = a3;

        result(x, y) = select(x == 0, b0,
                              select(x == 1, b1,
                                     select(x == 2, b2, b3)));

        result.compute_at(parallel_stage, y).bound(x, 0, 4).unroll(x);

        stages.push_back(result);
    }

    // Now build up to the total size, merging each pair of rows
    for (int chunk_size = 4; chunk_size < total_size; chunk_size *= 2) {
        // "result" contains the sorted halves
        assert(result.dimensions() == 2);

        // Merge pairs of rows from the partial result
        Func merge_rows("merge_rows");
        RDom r(0, chunk_size * 2);

        // The first dimension of merge_rows is within the chunk, and the
        // second dimension is the chunk index.  Keeps track of two
        // pointers we're merging from and an output value.
        merge_rows(x, y) = Tuple(0, 0, cast(input.value().type(), 0));

        Expr candidate_a = merge_rows(r - 1, y)[0];
        Expr candidate_b = merge_rows(r - 1, y)[1];
        Expr valid_a = candidate_a < chunk_size;
        Expr valid_b = candidate_b < chunk_size;
        Expr value_a = result(clamp(candidate_a, 0, chunk_size - 1), 2 * y);
        Expr value_b = result(clamp(candidate_b, 0, chunk_size - 1), 2 * y + 1);
        merge_rows(r, y) = tuple_select(valid_a && ((value_a < value_b) || !valid_b),
                                        Tuple(candidate_a + 1, candidate_b, value_a),
                                        Tuple(candidate_a, candidate_b + 1, value_b));

        if (chunk_size <= parallel_work_size) {
            merge_rows.compute_at(parallel_stage, y);
        } else {
            merge_rows.compute_root();
        }

        if (chunk_size == parallel_work_size) {
            parallel_stage(x, y) = merge_rows(x, y)[2];
            parallel_stage.compute_root().parallel(y);
            result = parallel_stage;
        } else {
            result = lambda(x, y, merge_rows(x, y)[2]);
        }
    }

    // Convert back to 1D
    return lambda(x, result(x, 0));
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    const int N = 1 << 10;

    Buffer<int> data(N);
    for (int i = 0; i < N; i++) {
        data(i) = rand() & 0xfffff;
    }
    Func input = lambda(x, data(x));

    printf("Bitonic sort...\n");
    Func f = bitonic_sort(input, N);
    f.bound(x, 0, N);
    f.compile_jit();
    printf("Running...\n");
    Buffer<int> bitonic_sorted(N);
    f.realize(bitonic_sorted);
    double t_bitonic = benchmark([&]() {
        f.realize(bitonic_sorted);
    });

    printf("Merge sort...\n");
    f = merge_sort(input, N);
    f.bound(x, 0, N);
    f.compile_jit();
    printf("Running...\n");
    Buffer<int> merge_sorted(N);
    f.realize(merge_sorted);
    double t_merge = benchmark([&]() {
        f.realize(merge_sorted);
    });

    Buffer<int> correct(N);
    for (int i = 0; i < N; i++) {
        correct(i) = data(i);
    }
    printf("std::sort...\n");
    double t_std = benchmark([&]() {
        std::sort(&correct(0), &correct(N));
    });

    printf("Times:\n"
           "bitonic sort: %fms \n"
           "merge sort: %fms \n"
           "std::sort %fms\n",
           t_bitonic * 1e3, t_merge * 1e3, t_std * 1e3);

    if (N <= 100) {
        for (int i = 0; i < N; i++) {
            printf("%8d %8d %8d\n",
                   correct(i), bitonic_sorted(i), merge_sorted(i));
        }
    }

    for (int i = 0; i < N; i++) {
        if (bitonic_sorted(i) != correct(i)) {
            printf("bitonic sort failed: %d -> %d instead of %d\n", i, bitonic_sorted(i), correct(i));
            return 1;
        }
        if (merge_sorted(i) != correct(i)) {
            printf("merge sort failed: %d -> %d instead of %d\n", i, merge_sorted(i), correct(i));
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

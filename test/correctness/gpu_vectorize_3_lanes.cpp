#include "Halide.h"
#include <stdio.h>

// Dense vector loads and stores of 3 lanes on the GPU, at indices that are
// provably multiples of 3.
//
// In OpenCL C a 3-component vector has the size and alignment of a
// 4-component one, so such an access can't be written as an access to an
// array of 3-component vectors: ((__global int3 *)p)[i] is the vector at
// p + 4 * i, not at p + 3 * i, and it spans 4 elements. Packed data of 3
// lanes has to go through vload3 and vstore3.

using namespace Halide;

template<typename T>
int test_global_memory(const char *type_name) {
    // A load from an input buffer and a store to the output buffer, both
    // with a min of 0, so their alignment is known.
    const int width = 48 * 8;
    ImageParam in(type_of<T>(), 1, "in");
    in.dim(0).set_min(0);

    Var x{"x"}, xo{"xo"}, xi{"xi"};
    Func out{"out"};
    out(x) = in(x)*cast<T>(2) + cast<T>(1);
    out.output_buffer().dim(0).set_min(0);
    out.gpu_tile(x, xo, xi, 16 * 3, TailStrategy::RoundUp)
        .vectorize(xi, 3);

    Buffer<T> input(width);
    for (int i = 0; i < width; i++) {
        input(i) = (T)(i % 1000);
    }
    in.set(input);

    Buffer<T> result = out.realize({width});
    result.copy_to_host();
    for (int i = 0; i < width; i++) {
        T correct = (T)(input(i) * 2 + 1);
        if (result(i) != correct) {
            printf("global memory, %s: out(%d) = %f instead of %f\n",
                   type_name, i, (double)result(i), (double)correct);
            return 1;
        }
    }
    return 0;
}

template<typename T>
int test_local_memory(const char *type_name) {
    // A producer staged per block in local (shared) memory, stored 3 lanes
    // at a time at multiples of 3, and read back both at multiples of 3
    // (f(x) and f(x + 3)) and off them (f(x + 1)).
    const int block = 48;
    const int width = block * 8;

    Var x{"x"}, xo{"xo"}, xi{"xi"}, xio{"xio"}, xii{"xii"};
    Func f{"f"}, g{"g"};
    f(x) = cast<T>(x % 1000) * cast<T>(2) + cast<T>(1);
    g(x) = f(x) + f(x + 1) + f(x + 3);
    g.output_buffer().dim(0).set_min(0);

    g.split(x, xo, xi, block, TailStrategy::RoundUp)
        .gpu_blocks(xo)
        .split(xi, xio, xii, 3)
        .gpu_threads(xio)
        .vectorize(xii);
    f.compute_at(g, xo)
        .split(x, xo, xi, 3, TailStrategy::RoundUp)
        .gpu_threads(xo)
        .vectorize(xi);

    Buffer<T> result = g.realize({width});
    result.copy_to_host();
    auto ref_f = [](int x) { return (T)((x % 1000) * 2 + 1); };
    for (int i = 0; i < width; i++) {
        T correct = (T)(ref_f(i) + ref_f(i + 1) + ref_f(i + 3));
        if (result(i) != correct) {
            printf("local memory, %s: g(%d) = %f instead of %f\n",
                   type_name, i, (double)result(i), (double)correct);
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    int failures = 0;
    failures += test_global_memory<uint16_t>("uint16");
    failures += test_global_memory<int32_t>("int32");
    failures += test_global_memory<float>("float");
    failures += test_local_memory<uint16_t>("uint16");
    failures += test_local_memory<int32_t>("int32");
    failures += test_local_memory<float>("float");
    if (failures != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}

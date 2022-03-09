#include "Halide.h"

using namespace Halide;

// https://github.com/halide/Halide/issues/6131
// Prior to the ClampUnsafeAccesses pass, this test case would
// crash as described in the comments below.

int main(int argc, char **argv) {
    Var x{"x"};
    {
        Func f{"f"}, g{"g"}, h{"h"}, out{"out"};

        const int min = -10000000;
        const int max = min + 20;

        h(x) = clamp(x, min, max);
        // Within its compute bounds, h's value will be within
        // [min,max]. Outside that, it's uninitialized memory.

        g(x) = sin(x);
        // Halide thinks g will be accessed within [min,max], so its
        // allocation bounds will be [min,max]

        f(x) = g(h(x));
        f.vectorize(x, 64, TailStrategy::RoundUp);
        // f will access h at values outside its compute bounds, and get
        // garbage, and then use that garbage to access g outside its
        // allocation bounds.

        out(x) = f(x);

        h.compute_root();
        g.compute_root();
        f.compute_root();

        out.realize({1});
    }

    {
        // A similar test, but with an input image, harvested from a real
        // failure in the wild.
        Buffer<uint8_t> input(1024);

        Func f;
        f(x) = clamp(12345234 / x, input.dim(0).min(), input.dim(0).max()) - 1234567890;

        Func g;
        // If f(x) is zero, this will read *way* outside of bounds.
        g(x) = input(f(x) + 1234567890);

        Func h;
        h(x) = g(x);

        f.compute_root();
        h.vectorize(x, 8, TailStrategy::GuardWithIf);
        // Massively over-compute g
        g.compute_at(h, x).bound_extent(x, 1024);

        h.realize({1024});
    }

    // No crash means success

    printf("Success!\n");
    return 0;
}

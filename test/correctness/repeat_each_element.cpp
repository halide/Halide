#include "Halide.h"
#include <stdio.h>

using namespace Halide;

// Vectorizing f(x / k) produces shuffles that repeat each element of a short
// vector k times, which show up in the IR as a transpose of a broadcast of a
// vector, or as an interleave of a vector with itself. Check that these
// compute the right thing for a variety of types, repeat factors and vector
// widths.

template<typename T>
bool test(int k, int lanes, int offset) {
    const int size = 1024;
    Buffer<T> in(size / k + offset + 1);
    for (int i = 0; i < in.width(); i++) {
        in(i) = (T)(i * 37 + 11);
    }

    Var x{"x"};
    Func f{"f"}, g{"g"};
    f(x) = in(x / k + offset);
    f.vectorize(x, lanes);
    // Also use the repeated vector in some arithmetic that depends on the
    // position within each run.
    g(x) = in(x / k + offset) * cast<T>(x % k + 1) + cast<T>(x);
    g.vectorize(x, lanes);

    Buffer<T> out_f(size), out_g(size);
    Pipeline({f, g}).realize({out_f, out_g});
    for (int i = 0; i < size; i++) {
        T correct_f = in(i / k + offset);
        T correct_g = (T)(correct_f * (T)(i % k + 1) + (T)i);
        if (out_f(i) != correct_f || out_g(i) != correct_g) {
            printf("k = %d, lanes = %d, offset = %d: "
                   "f(%d) = %f instead of %f, g(%d) = %f instead of %f\n",
                   k, lanes, offset,
                   i, (double)out_f(i), (double)correct_f,
                   i, (double)out_g(i), (double)correct_g);
            return false;
        }
    }
    return true;
}

bool test_bits(int lanes) {
    // Extract the bits of a densely-packed bit vector, one per lane.
    const int size = 1024;
    Buffer<uint8_t> in(size / 8);
    for (int i = 0; i < in.width(); i++) {
        in(i) = (uint8_t)(i * 73 + 5);
    }

    Var x{"x"};
    Func f{"f"};
    f(x) = (in(x / 8) >> cast<uint8_t>(x % 8)) & 1;
    f.vectorize(x, lanes);

    Buffer<uint8_t> out = f.realize({size});
    for (int i = 0; i < size; i++) {
        uint8_t correct = (in(i / 8) >> (i % 8)) & 1;
        if (out(i) != correct) {
            printf("bits, lanes = %d: out(%d) = %d instead of %d\n",
                   lanes, i, out(i), correct);
            return false;
        }
    }
    return true;
}

template<typename T>
bool test_all() {
    for (int k : {2, 3, 4, 8, 16}) {
        for (int lanes : {8, 16, 32, 64}) {
            for (int offset : {0, 3}) {
                if (!test<T>(k, lanes, offset)) {
                    return false;
                }
            }
        }
    }
    return true;
}

int main(int argc, char **argv) {
    if (!test_all<uint8_t>() ||
        !test_all<int8_t>() ||
        !test_all<uint16_t>() ||
        !test_all<int32_t>() ||
        !test_all<float>()) {
        return 1;
    }

    for (int lanes : {8, 16, 32, 64}) {
        if (!test_bits(lanes)) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

#include "Halide.h"

#include <iostream>
#include <random>

using namespace Halide;

int simple_popcount(uint8_t a) {
    int bits_set = 0;
    while (a != 0) {
        bits_set += a & 1;
        a >>= 1;
    }
    return bits_set;
}

int simple_count_leading_zeros(uint8_t a) {
    int leading_zeros = 0;
    int bit = 7;
    while (bit >= 0 && (a & (1 << bit)) == 0) {
        leading_zeros++;
        bit--;
    }
    return leading_zeros;
}

int simple_count_trailing_zeros(uint8_t a) {
    int trailing_zeros = 0;
    int bit = 0;
    while (bit < 8 && (a & (1 << bit)) == 0) {
        trailing_zeros++;
        bit++;
    }
    return trailing_zeros;
}

int main(int argc, char **argv) {
    ImageParam in(UInt(8), 1);
    Buffer<uint8_t> mapping(9);
    int i = 0;
    for (uint8_t v : {4, 2, 8, 5, 1, 7, 0, 3, 6}) {  // Random permutation of 0..7
        mapping(i++) = v;
    }

    for (int vectorize = 0; vectorize <= 1; vectorize++) {
        if (vectorize && get_jit_target_from_environment().arch != Target::X86) {
            // Not all architectures support vectorized popc/ctz/clz operations,
            // and will fail at LLVM time. Skipping for non-x86 for now.
            continue;
        }

        Var x;
        Func f;
        f(x) = Tuple(mapping(popcount(in(x))),
                     mapping(count_leading_zeros(in(x))),
                     mapping(count_trailing_zeros(in(x))));

        if (vectorize) {
            f.vectorize(x, 8);
        }

        std::mt19937 rng(0);
        Buffer<uint8_t> data(16);
        for (int32_t i = 0; i < 16; i++) {
            data(i) = rng();
        }
        in.set(data);

        Realization result = f.realize(16);
        Buffer<uint8_t> popc_result = result[0];
        Buffer<uint8_t> clz_result = result[1];
        Buffer<uint8_t> ctz_result = result[2];

        for (int32_t i = 0; i < 16; i++) {
            assert(popc_result(i) == mapping(simple_popcount(data(i))));
            assert(clz_result(i) == mapping(simple_count_leading_zeros(data(i))));
            assert(ctz_result(i) == mapping(simple_count_trailing_zeros(data(i))));
        }
    }

    std::cout << "Success!\n";
    return 0;
}

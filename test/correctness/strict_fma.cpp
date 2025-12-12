#include "Halide.h"

using namespace Halide;

template<typename T, typename Bits>
int test() {
    std::cout << "Testing " << type_of<T>() << "\n";
    Func f{"f"}, g{"g"};
    Param<T> b{"b"}, c{"c"};
    Var x{"x"};

    f(x) = fma(cast<T>(x), b, c);
    g(x) = strict_float(cast<T>(x) * b + c);

    // Use a non-native vector width, to also test legalization
    f.vectorize(x, 5);
    g.vectorize(x, 5);

    // b.set((T)8769132.122433244233);
    // c.set((T)2809.14123423413);
    b.set((T)1.111111111);
    c.set((T)1.101010101);
    Buffer<T> with_fma = f.realize({1024});
    Buffer<T> without_fma = g.realize({1024});

    bool saw_error = false;
    for (int i = 0; i < with_fma.width(); i++) {
        if (with_fma(i) == without_fma(i)) {
            continue;
        }

        saw_error = true;
        // The rounding error, if any, ought to be 1 ULP
        Bits fma_bits = Internal::reinterpret_bits<Bits>(with_fma(i));
        Bits no_fma_bits = Internal::reinterpret_bits<Bits>(without_fma(i));
        if (fma_bits + 1 != no_fma_bits &&
            fma_bits - 1 != no_fma_bits) {
            printf("Difference greater than 1 ULP: %10.10g (0x%llx) vs %10.10g (0x%llx)!\n",
                   (double)with_fma(i), (long long unsigned)fma_bits,
                   (double)without_fma(i), (long long unsigned)no_fma_bits);
            return -1;
        }
    }

    if (!saw_error) {
        printf("There should have occasionally been a 1 ULP difference between fma and non-fma results\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {

    if (test<double, uint64_t>() ||
        test<float, uint32_t>() ||
        test<float16_t, uint16_t>()) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}

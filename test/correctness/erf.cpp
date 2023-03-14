#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int bits_diff(float fa, float fb) {
    uint32_t a = Halide::Internal::reinterpret_bits<uint32_t>(fa);
    uint32_t b = Halide::Internal::reinterpret_bits<uint32_t>(fb);
    uint32_t a_exp = a >> 23;
    uint32_t b_exp = b >> 23;
    if (a_exp != b_exp) return -100;
    uint32_t diff = a > b ? a - b : b - a;
    int count = 0;
    while (diff) {
        count++;
        diff /= 2;
    }
    return count;
}

int main(int argc, char **argv) {
    Func f;
    Var x;

    f(x) = erf((x - 50000) / 10000.0f);
    f.vectorize(x, 8);

    Buffer<float> im = f.realize({100000});

    int max_err = 0;
    float max_err_x = 0;
    for (int i = 0; i < 100000; i++) {
        float x = (i - 50000) / 10000.0f;
        float correct = erff(x);
        float approx = im(i);
        int err = bits_diff(correct, approx);
        if (err > max_err) {
            max_err = err;
            max_err_x = x;
        }
    }

    printf("Maximum number of incorrect mantissa bits: %d @ %g\n", max_err, max_err_x);

    if (max_err <= 4) {
        printf("Success!\n");
        return 0;
    } else {
        return 1;
    }
}

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Halide only provides 32-bit noise functions, which are overkill for
    // generating low bit-depth noise (e.g. for dithering). This test shows how
    // to generate 8-bit noise by slicing out bytes from 32-bit noise.
    Var x;

    Func noise;
    noise(x) = random_uint();

    Func noise8;
    noise8(x) = extract_bits<uint8_t>(noise(x / 4), 8 * (x % 4));

    Func in16;
    in16(x) = cast<uint16_t>(x);

    Func dithered;
    dithered(x) = cast<uint8_t>((in16(x) + noise8(x)) >> 8);

    in16.compute_root();
    dithered.compute_root().vectorize(x, 16, TailStrategy::RoundUp);
    noise8.compute_at(dithered, x).vectorize(x);

    // To keep things aligned:
    dithered.output_buffer().dim(0).set_min(0);

    Buffer<uint8_t> out = dithered.realize({1 << 15});

    uint32_t sum = 0, correct_sum = 0;
    for (int i = 0; i < out.width(); i++) {
        sum += out(i);
        correct_sum += i;
    }
    correct_sum = (correct_sum + 128) >> 8;

    if (std::abs((double)sum - correct_sum) / correct_sum > 1e-4) {
        printf("Suspiciously large relative difference between the sum of the dithered values and the full-precision sum: %d vs %d\n", sum, correct_sum);
        return 1;
    }

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <limits>

using namespace Halide;
using namespace Halide::Internal;

// Test vectorized long_div implementation for Hexagon DSP.
template<typename ITYPE>
bool test() {
    const Target target = get_jit_target_from_environment();
    const int W_img = 1024;
    const int H_img = 1024;

    srand(time(0));

    Buffer<ITYPE> num(W_img, H_img);
    Buffer<ITYPE> den(W_img, H_img);
    for (int y = 0; y < H_img; y++) {
        for (int x = 0; x < W_img; x++) {
            num(x, y) = (ITYPE)rand();
            den(x, y) = (ITYPE)rand();
        }
    }
    // Make sure we have corner cases covered:
    // 1. den == 0
    den(0, 0) = (ITYPE)0;
    // 2. den == -1 && num = signed_min
    if (num.type().is_int()) {
        num(1, 0) = (ITYPE)std::numeric_limits<ITYPE>::min();
        den(1, 0) = (ITYPE)-1;
    }

    Func out;
    Var x, y;

    out(x, y) = num(x, y)/den(x, y);

    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        Var yi;
        out
            .hexagon()
            .vectorize(x, vector_size);
    }

    Buffer<ITYPE> output_buf = out.realize(W_img, H_img);

    for (int y = 0; y < H_img; y++) {
        for (int x = 0; x < W_img; x++) {
            ITYPE correct = div_imp(num(x, y), den(x, y));
            if (output_buf(x, y) != correct) {
                printf("output(%d, %d) = %d instead of %d (%d/%d)\n",
                        x, y, output_buf(x, y), correct, num(x, y), den(x, y));
                return false;
            }
        }
    }

    return true;
}

int main() {
    if (!test<uint8_t>() ||
        !test<int8_t>() ||
        !test<uint16_t>() ||
        !test<int16_t>() ||
        !test<uint32_t>() ||
        !test<int32_t>()) return 1;
    printf("Success!\n");
    return 0;
}

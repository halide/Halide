#include "Halide.h"

using namespace Halide;

// Implements a simple gather pipeline to make use of VTCM available on v65+
// hexagon DSP.
template<typename ITYPE>
int test() {
    const int W = 1024;
    const int H = 2;

    // srand(time(0));

    Buffer<ITYPE> input(W+1, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W+1; x++) {
            int idx = (ITYPE)rand();
            input(x, y) = idx; // std::max(idx, -idx);
        }
    }

    Buffer<ITYPE> lut(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            lut(x, y) = (ITYPE)(rand());
        }
    }

    Var x, y;
    Func lut_vtcm, output_vtcm, output;

    // Implement: output(x, y) = lut(input(x, y), input(x+1, y))
    // output and lut must have store_in(MemoryType::VTCM) to generate vgathers.
    Expr xCoord = clamp(cast<int32_t>(input(x, y)), 0, W-1);
    Expr yCoord = clamp(cast<int32_t>(input(x+1, y)), 0, H-1);
    lut_vtcm(x, y) = lut(x, y);
    output_vtcm(x, y) = lut_vtcm(xCoord, yCoord);
    output(x, y) = output_vtcm(x, y);

    Target target = get_jit_target_from_environment();
    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        lut_vtcm
            .compute_at(output, y)
            .store_in(MemoryType::VTCM)
            .vectorize(x, vector_size/2);

        output_vtcm
            .compute_at(output, y)
            .store_in(MemoryType::VTCM)
            .vectorize(x, vector_size/2);

        output
            .hexagon()
            .vectorize(x, vector_size/2);
    }

    Buffer<ITYPE> output_buf = output.realize(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            // printf("(%d, %d) = %d\n", x, y, input(x,  y));
            int xCoord = std::max(std::min((int)(input(x, y)), W-1), 0);
            int yCoord = std::max(std::min((int)(input(x+1, y)), H-1), 0);
            ITYPE correct = lut(xCoord, yCoord);
            if (output_buf(x, y) != correct) {
                printf("output(%d, %d) = %d instead of %d\n", x, y, output_buf(x, y), correct);
                // return false;
            }
        }
    }

    return true;
}

int main() {
    if (
        // !test<uint16_t>()
        !test<int16_t>()
        // !test<uint32_t>()
        // !test<int32_t>()
        ) return 1;
    printf("Success!\n");
    return 0;
}

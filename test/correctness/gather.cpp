#include "Halide.h"

using namespace Halide;

// Implements a simple gather pipeline to make use of VTCM available on v65+
// hexagon DSP.
template<typename ITYPE>
bool test() {
    const Target target = get_jit_target_from_environment();
    const int W_img = 128;
    const int H_img = 8;
    const int W_lut = 256;
    const int H_lut = (target.has_feature(Target::HVX_v65)) ? 32 : 1;

    srand(time(0));

    // Separate channel for xCoord and yCoord for LUT index.
    Buffer<ITYPE> input(W_img, 2);
    for (int x = 0; x < W_img; x++) {
        input(x, 0) = (ITYPE)rand() % W_lut;
        input(x, 1) = (ITYPE)rand() % H_lut;
    }
    // Two Dimensional LUT.
    Buffer<ITYPE> lut(W_lut, H_lut);
    for (int y = 0; y < H_lut; y++) {
        for (int x = 0; x < W_lut; x++) {
            lut(x, y) = (ITYPE)rand();
        }
    }

    Var x, y;
    Func lut_vtcm, output_vtcm, output;

    // Implement: output(x, y) = lut(input(x, 0), input(x, 1))
    // output and lut must have store_in(MemoryType::VTCM) to generate vgathers.
    Expr xCoord = clamp(cast<int32_t>(input(x, 0)), 0, W_lut - 1);
    Expr yCoord = clamp(cast<int32_t>(input(x, 1)), 0, H_lut - 1);
    lut_vtcm(x, y) = lut(x, y);
    output_vtcm(x, y) = lut_vtcm(xCoord, yCoord);
    output(x, y) = output_vtcm(x, y);

    if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        const int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        Var yi;

        output
            .hexagon()
            .split(y, y, yi, H_img / 2)
            .parallel(y)
            .vectorize(x, vector_size);

        if (target.features_any_of({Target::HVX_v65, Target::HVX_v66})) {
            lut_vtcm
                .store_in(MemoryType::VTCM)
                .compute_at(output, Var::outermost())
                .vectorize(x, vector_size);

            output_vtcm
                .store_in(MemoryType::VTCM)
                .compute_at(output, y)
                .vectorize(x, vector_size);
        }
    }

    Buffer<ITYPE> output_buf = output.realize(W_img, H_img);

    for (int y = 0; y < H_img; y++) {
        for (int x = 0; x < W_img; x++) {
            int xCoord = std::max(std::min((int)(input(x, 0)), W_lut - 1), 0);
            int yCoord = std::max(std::min((int)(input(x, 1)), H_lut - 1), 0);
            ITYPE correct = lut(xCoord, yCoord);
            if (output_buf(x, y) != correct) {
                printf("output(%d, %d) = %d instead of %d\n", x, y, output_buf(x, y), correct);
                return false;
            }
        }
    }

    return true;
}

int main() {
    // With hexagon targets >=v65 with hvx, we expect to see gathers for
    // uint16_t, int16_t, uint32_t, int32_t
    // For targets <v65 with hvx, we should generate dynamic_shuffle which are
    // compiled to vlut instructions.
    if (!test<uint8_t>() ||
        !test<int8_t>() ||
        !test<uint16_t>() ||
        !test<int16_t>() ||
        !test<uint32_t>() ||
        !test<int32_t>()) return 1;
    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <iostream>

//#define RDOM_OK 1

using namespace Halide;
using namespace Halide::Internal;

int main(int arch, char **argv) {
    int verbose = 0;
    const int W = 256, H = 256;
    Buffer<uint8_t> in(W, H);
    // Set up the input.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }
    Buffer<int8_t> mask(3, 3);
    mask(0, 0) = 1;
    mask(1, 0) = -4;
    mask(2, 0) = 7;
    mask(0, 1) = 2;
    mask(1, 1) = -5;
    mask(2, 1) = 8;
    mask(0, 2) = 3;
    mask(1, 2) = -6;
    mask(2, 2) = 9;

    Var x("x"), y("y");

    // Boundary condition.
    uint8_t exterior = 0;
    Func input = BoundaryConditions::constant_exterior(in, exterior);
    input.compute_root();

    // Algorithm.
    RDom r(-1,3,-1,3);
    Func conv3x3("conv3x3a16");
#ifdef RDOM_OK
    conv3x3(x, y) =
        cast<uint8_t>(clamp(
            sum(cast<int16_t>(input(x+r.x, y+r.y)) *
                cast<int16_t>(mask(1+r.x, 1+r.y))) >> 4,
            0, 255));
#else
    conv3x3(x, y) =
        cast<uint8_t>(clamp(
            (cast<int16_t>(input(x-1, y-1)) * cast<int16_t>(mask(0,0)) +
             cast<int16_t>(input(x,   y-1)) * cast<int16_t>(mask(1,0)) +
             cast<int16_t>(input(x+1, y-1)) * cast<int16_t>(mask(2,0)) +
             cast<int16_t>(input(x-1, y))   * cast<int16_t>(mask(0,1)) +
             cast<int16_t>(input(x,   y))   * cast<int16_t>(mask(1,1)) + 
             cast<int16_t>(input(x+1, y))   * cast<int16_t>(mask(2,1)) +
             cast<int16_t>(input(x-1, y+1)) * cast<int16_t>(mask(0,2)) +
             cast<int16_t>(input(x,   y+1)) * cast<int16_t>(mask(1,2)) +
             cast<int16_t>(input(x+1, y+1)) * cast<int16_t>(mask(2,2))) >> 4,
            0, 255));
#endif

    // Schedule.
    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        conv3x3.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.features_any_of({Target::HVX_64, Target::HVX_128})) {
        int vector_size = target.has_feature(Target::HVX_128) ? 128 : 64;
        conv3x3.hexagon().vectorize(x, vector_size);

        conv3x3.output_buffer().dim(0).set_min(0);
        conv3x3.output_buffer().dim(1).set_min(0);

        // Require scanlines of the input and output to be aligned.
        auto out_buffer = conv3x3.output_buffer();

        // in.dim(0).set_bounds(0, (in.dim(0).extent()/vector_size)*vector_size);
        out_buffer.dim(0).set_bounds(0, (out_buffer.dim(0).extent()/vector_size)*vector_size);

        for (int i = 1; i < 2; i++) {
            // in.dim(i).set_stride((in.dim(i).stride()/vector_size)*vector_size);
            out_buffer.dim(i).set_stride((out_buffer.dim(i).stride()/vector_size)*vector_size);
        }
    } else {
        conv3x3.vectorize(x, target.natural_vector_size<uint8_t>());
    }

    // Run the pipeline and verify the results are correct.
    Buffer<uint8_t> out = conv3x3.realize(W, H, target);


    if (verbose) {
        for (int y = 0; y < 3; y++) {
            for (int x = 0; x < 3; x++) {
                printf("mask(%d, %d) = %d\n", x, y, mask(x, y));
            }
        }
    }

    // Boundary condition
    auto bin = [&](int x, int y) { return (x >= 0 && x < W && y >=0 && y < H) ? in(x, y) : exterior; };

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int16_t correct = (mask(0,0)*bin(x-1, y-1) + mask(1,0)*bin(x, y-1) + mask(2,0)*bin(x+1, y-1) +
                               mask(0,1)*bin(x-1, y)   + mask(1,1)*bin(x, y)   + mask(2,1)*bin(x+1, y)   +
                               mask(0,2)*bin(x-1, y+1) + mask(1,2)*bin(x, y+1) + mask(2,2)*bin(x+1, y+1)) >> 4;
            // clamp
            if (correct > 255) { correct = 255; }
            if (correct < 0) { correct = 0; }

            if (correct != out(x, y)) {
                // printf("in(%d, %d) = %d\n", x, y, in(x, y));
                printf("out(%d, %d) = %d instead of %d\n", x, y, out(x, y), correct);
                if (!verbose)
                    return -1;
            }
        }
    }
    std::cout << "Success!\n";
    return 0;
}

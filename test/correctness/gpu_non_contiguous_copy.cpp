#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    Var x, y, z, w;
    Buffer<int> full(80, 60, 10, 10);

    const int x_off = 4, y_off = 8, z_off = 2, w_off = 4;
    const int x_size = 16, y_size = 16, z_size = 3, w_size = 3;

    // We want to construct a new Buffer that refers to the same data
    // but a different halide_buffer_t.
    Buffer<int> cropped(*full.raw_buffer());
    cropped.raw_buffer()->host = (uint8_t *)&(full(x_off, y_off, z_off, w_off));
    cropped.raw_buffer()->dim[0].extent = x_size;
    cropped.raw_buffer()->dim[1].extent = y_size;
    cropped.raw_buffer()->dim[2].extent = z_size;
    cropped.raw_buffer()->dim[3].extent = w_size;
    cropped.raw_buffer()->dim[0].stride *= 2;
    cropped.raw_buffer()->dim[1].stride *= 2;
    cropped.raw_buffer()->dim[2].stride *= 2;
    cropped.raw_buffer()->dim[3].stride *= 2;

    // Make a bitmask representing the region inside the crop.
    Buffer<bool> in_subregion(80, 60, 10, 10);
    Expr test = ((x >= x_off) && (x < x_off + x_size * 2) &&
                 (y >= y_off) && (y < y_off + y_size * 2) &&
                 (z >= z_off) && (z < z_off + z_size * 2) &&
                 (w >= w_off) && (w < w_off + w_size * 2) &&
                 (x % 2 == 0) &&
                 (y % 2 == 0) &&
                 (z % 2 == 0) &&
                 (w % 2 == 0));
    Func test_func;
    test_func(x, y, z, w) = test;
    test_func.realize(in_subregion);

    Func f;
    f(x, y, z, w) = 3 * x + 2 * y + z + 4 * w;
    if (target.has_gpu_feature()) {
        Var xi, yi;
        f.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon().vectorize(x, 16);
    }
    f.output_buffer().dim(0).set_stride(Expr());
    f.realize(cropped);

    // Put some data in the full host buffer, avoiding the region
    // being evaluated above.
    Expr change_out_of_subregion = select(test, undef<int>(), 4 * x + 3 * y + 2 * z + w);
    lambda(x, y, z, w, change_out_of_subregion).realize(full);

    // Copy back the output subset from the GPU.
    cropped.copy_to_host();

    for (int w = 0; w < full.extent(3); ++w) {
        for (int z = 0; z < full.extent(2); ++z) {
            for (int y = 0; y < full.extent(1); ++y) {
                for (int x = 0; x < full.extent(0); ++x) {
                    int correct;
                    if (in_subregion(x, y, z, w)) {
                        int x_ = (x - x_off) / 2;
                        int y_ = (y - y_off) / 2;
                        int z_ = (z - z_off) / 2;
                        int w_ = (w - w_off) / 2;
                        correct = 3 * x_ + 2 * y_ + z_ + 4 * w_;
                    } else {
                        correct = 4 * x + 3 * y + 2 * z + w;
                    }
                    if (full(x, y, z, w) != correct) {
                        printf("Error! Incorrect value %i != %i at %i, %i, %i, %i\n", full(x, y, z, w), correct, x, y, z, w);
                        return 1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

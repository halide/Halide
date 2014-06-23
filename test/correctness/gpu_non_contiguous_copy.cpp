#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z, w;
    Image<int> full(80, 60, 10, 10);

    buffer_t cropped = *full.raw_buffer();
    cropped.host = (uint8_t *)&(full(4, 8, 2, 4));
    cropped.min[0] = 0;
    cropped.min[1] = 0;
    cropped.min[2] = 0;
    cropped.min[3] = 0;
    cropped.extent[0] = 16;
    cropped.extent[1] = 16;
    cropped.extent[2] = 3;
    cropped.extent[3] = 3;
    cropped.stride[0] *= 2;
    cropped.stride[1] *= 2;
    cropped.stride[2] *= 2;
    cropped.stride[3] *= 2;
    Buffer out(Int(32), &cropped);

    Func f;

    f(x, y, z, w) = 3*x + 2*y + z + 4*w;
    f.gpu_tile(x, y, 16, 16);
    f.output_buffer().set_stride(0, Expr());
    f.realize(out);

    // Put some data in the full host buffer.
    lambda(x, y, z, w, 4*x + 3*y + 2*z + w).realize(full);

    // Copy back the output subset from the GPU.
    out.copy_to_host();

    for (int w = 0; w < full.extent(3); ++w) {
        for (int z = 0; z < full.extent(2); ++z) {
            for (int y = 0; y < full.extent(1); ++y) {
                for (int x = 0; x < full.extent(0); ++x) {
                    int correct = 4*x + 3*y + 2*z + w;

                    int w_ = (w - 4)/2;
                    int z_ = (z - 2)/2;
                    int y_ = (y - 8)/2;
                    int x_ = (x - 4)/2;

                    if (cropped.min[3] <= w_ && w_ < cropped.min[3] + cropped.extent[3] &&
                        cropped.min[2] <= z_ && z_ < cropped.min[2] + cropped.extent[2] &&
                        cropped.min[1] <= y_ && y_ < cropped.min[1] + cropped.extent[1] &&
                        cropped.min[0] <= x_ && x_ < cropped.min[0] + cropped.extent[0] &&
                        x % 2 == 0 && y % 2 == 0 && z % 2 == 0 && w % 2 == 0) {
                        correct = 3*x_ + 2*y_ + z_ + 4*w_;
                    }
                    if (full(x, y, z, w) != correct) {
                        printf("Error! Incorrect value %i != %i at %i, %i, %i, %i\n", full(x, y, z, w), correct, x, y, z, w);
                        return -1;
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

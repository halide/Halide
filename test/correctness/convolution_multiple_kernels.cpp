#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(ConvolutionMultipleKernelsTest, BoxBlurCorrectness) {
    const int W = 64, H = 16;

    Buffer<uint16_t> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() & 0xff;
        }
    }

    Var x("x"), y("y");

    Func input("input");
    input(x, y) = in(clamp(x, 0, W - 1), clamp(y, 0, H - 1));
    input.compute_root();

    // The kernels in this test are just simple box blurs.
    Func box1, box2;
    box1(x, y) = cast<uint16_t>(1);
    // Make this other box uint32 so its buffer is a different size.
    box2(x, y) = cast<uint32_t>(2);
    // Compute the kernels outside of blur. If blur is scheduled on the GPU,
    // the buffers for these Funcs should be passed as constant memory
    // if possible.
    box1.compute_root();
    box2.compute_root();

    // Compute the sum of the convolution of the image with both kernels.
    Func blur("blur");
    RDom r(-1, 3, -1, 3);
    blur(x, y) = sum(box1(r.x, r.y) * input(x + r.x, y + r.y)) +
                 sum(cast<uint16_t>(box2(r.x, r.y)) * input(x + r.x, y + r.y));

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi("xi"), yi("yi");
        blur.gpu_tile(x, y, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        blur.hexagon().vectorize(x, 64);
    }

    Buffer<uint16_t> out = blur.realize({W, H}, target);

    for (int y = 2; y < H - 2; y++) {
        for (int x = 2; x < W - 2; x++) {
            uint16_t correct = (in(x - 1, y - 1) + in(x, y - 1) + in(x + 1, y - 1) +
                                in(x - 1, y) + in(x, y) + in(x + 1, y) +
                                in(x - 1, y + 1) + in(x, y + 1) + in(x + 1, y + 1)) *
                               3;
            ASSERT_EQ(out(x, y), correct) << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << correct;
        }
    }
}

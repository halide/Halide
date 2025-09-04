#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(GPUVectorize, BasicVectorization) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");

    f(x, y) = x * y + 2.4f;

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::GuardWithIf).vectorize(xi, 4, TailStrategy::GuardWithIf);
    }

    Buffer<float> imf = f.realize({32, 32}, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = i * j + 2.4f;
            ASSERT_NEAR(imf(i, j), correct, 0.001f) << "at (" << i << ", " << j << ")";
        }
    }
}

TEST(GPUVectorize, VectorizeWithImageParam) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");
    ImageParam im(Float(32), 2);

    f(x, y) = x * y + 2.4f + im(x, y);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::GuardWithIf).vectorize(xi, 4, TailStrategy::GuardWithIf);
    }

    Buffer<float> input_img(32, 32);
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            input_img(i, j) = i + j;
        }
    }
    im.set(input_img);

    Buffer<float> imf = f.realize({32, 32}, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = i * j + 2.4f + i + j;
            ASSERT_NEAR(imf(i, j), correct, 0.001f) << "at (" << i << ", " << j << ")";
        }
    }
}

TEST(GPUVectorize, VectorizeWithSelect) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func f("f");
    ImageParam im(Float(32), 2);

    f(x, y) = select(im(x, y) > 32.0f, 1.0f, -1.0f) + im(x, y);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::GuardWithIf).vectorize(xi, 4, TailStrategy::GuardWithIf);
    }

    Buffer<float> input_img(32, 32);
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            input_img(i, j) = i + j;
        }
    }
    im.set(input_img);

    Buffer<float> imf = f.realize({32, 32}, target);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (i + j > 32 ? 1.0f : -1.0f) + i + j;
            ASSERT_NEAR(imf(i, j), correct, 0.001f) << "at (" << i << ", " << j << ")";
        }
    }
}

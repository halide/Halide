#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

class MultipleOutputsTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    bool use_gpu{target.has_gpu_feature()};
};

TEST_F(MultipleOutputsTest, InternalFuncWithMultipleValues) {
    // An internal Func that produces multiple values.
    Func f, g;
    Var x, xi;
    f(x) = {x, sin(x)};

    f.compute_root();

    Tuple t = f(x);
    g(x) = t[0] + t[1];

    if (use_gpu) {
        g.gpu_tile(x, xi, 8);
    }

    ASSERT_NO_THROW(g.realize({100}));
}

TEST_F(MultipleOutputsTest, TupleReduction) {
    // Now try a reduction where the pipeline returns that tuple value.
    Func f, g;
    Var x, y;
    f(x, y) = sin(x * y);
    f.compute_root();

    // Find argmax of f over [0, 100]^2
    RDom r(0, 100, 0, 100);

    g() = Tuple(0, 0, f(0, 0));

    Expr best_x = g()[0], best_y = g()[1], best_so_far = g()[2];
    Expr next_value = f(r.x, r.y);
    g() = select(next_value > best_so_far,
                 {r.x, r.y, next_value},
                 {best_x, best_y, best_so_far});

    if (use_gpu) {
        g.gpu_single_thread();
    }

    Realization result = g.realize();
    // int result_x = Buffer<int>(result[0])(0);
    // int result_y = Buffer<int>(result[1])(0);
    float result_val = Buffer<float>(result[2])(0);
    ASSERT_NEAR(result_val, 1.0f, 1e-5) << "argmax{sin(x*y)}";
}

TEST_F(MultipleOutputsTest, DifferentSizes) {
    // Now multiple output Funcs with different sizes
    Func f, g;
    Var x, xi;
    f(x) = 100 * x;
    g(x) = x;

    if (use_gpu) {
        f.gpu_tile(x, xi, 8);
        g.gpu_tile(x, xi, 8);
    }

    Buffer<int> f_im(100);
    Buffer<int> g_im(10);
    Pipeline({f, g}).realize({f_im, g_im});

    if (use_gpu) {
        ASSERT_TRUE(f_im.device_dirty() && g_im.device_dirty());
        f_im.copy_to_host();
        g_im.copy_to_host();
    }

    for (int x = 0; x < f_im.width(); x++) {
        ASSERT_EQ(f_im(x), 100 * x) << "f(" << x << ") = " << f_im(x) << " instead of " << 100 * x;
    }

    for (int x = 0; x < g_im.width(); x++) {
        ASSERT_EQ(g_im(x), x) << "g(" << x << ") = " << g_im(x) << " instead of " << x;
    }
}

TEST_F(MultipleOutputsTest, InferredRealization) {
    // Now multiple output Funcs via inferred Realization
    Func f, g;
    Var x, xi;
    f(x) = cast<float>(100 * x);
    g(x) = Tuple(cast<uint8_t>(x), cast<int16_t>(x + 1));

    if (use_gpu) {
        f.gpu_tile(x, xi, 8);
        g.gpu_tile(x, xi, 8);
    }

    Realization r = Pipeline({f, g}).realize({100});
    Buffer<float> f_im = r[0];
    Buffer<uint8_t> g0_im = r[1];
    Buffer<int16_t> g1_im = r[2];

    for (int x = 0; x < f_im.width(); x++) {
        ASSERT_EQ(f_im(x), 100 * x) << "f(" << x << ") = " << f_im(x) << " instead of " << (float)100 * x;
    }

    for (int x = 0; x < g0_im.width(); x++) {
        ASSERT_EQ(g0_im(x), x) << "g0(" << x << ") = " << (int)g0_im(x) << " instead of " << x;
    }

    for (int x = 0; x < g1_im.width(); x++) {
        ASSERT_EQ(g1_im(x), x + 1) << "g1(" << x << ") = " << (int)g1_im(x) << " instead of " << (x + 1);
    }
}

TEST_F(MultipleOutputsTest, DifferentDimensionalitiesAndTuples) {
    // Multiple output Funcs of different dimensionalities that call each other and some of them are Tuples.
    Func f, g, h;
    Var x, y, xi, yi;

    f(x) = x;
    h(x) = {f(x) + 17, f(x) - 17};
    g(x, y) = {f(x + y) * 2, h(x)[0] * y, h(x)[1] - 2};

    if (get_jit_target_from_environment().has_gpu_feature()) {
        g.gpu_tile(x, y, xi, yi, 1, 1);
    }

    Buffer<int> f_im(100), g_im0(20, 20), g_im1(20, 20), g_im2(20, 20), h_im0(50), h_im1(50);

    Pipeline({h, g, f}).realize({h_im0, h_im1, g_im0, g_im1, g_im2, f_im});

    if (use_gpu) {
        // g should have been written on the device
        ASSERT_TRUE(g_im0.device_dirty() &&
                    g_im1.device_dirty() &&
                    g_im2.device_dirty());
        // f and h should have been copied to the device for g to read
        ASSERT_TRUE(f_im.has_device_allocation() &&
                    h_im0.has_device_allocation() &&
                    h_im1.has_device_allocation());
        g_im0.copy_to_host();
        g_im1.copy_to_host();
        g_im2.copy_to_host();
    }

    for (int x = 0; x < 100; x++) {
        ASSERT_EQ(f_im(x), x) << "f(" << x << ") = " << f_im(x) << " instead of " << x;
        if (x < 50) {
            int c0 = f_im(x) + 17;
            int c1 = f_im(x) - 17;
            ASSERT_EQ(h_im0(x), c0) << "h(" << x << ")[0] = " << h_im0(x) << " instead of " << c0;
            ASSERT_EQ(h_im1(x), c1) << "h(" << x << ")[1] = " << h_im1(x) << " instead of " << c1;
        }
        if (x < 20) {
            for (int y = 0; y < 20; y++) {
                int c0 = f_im(x + y) * 2;
                int c1 = h_im0(x) * y;
                int c2 = h_im1(x) - 2;
                ASSERT_EQ(g_im0(x, y), c0) << "g(" << x << ", " << y << ")[0] = " << g_im0(x, y) << " instead of " << c0;
                ASSERT_EQ(g_im1(x, y), c1) << "g(" << x << ", " << y << ")[1] = " << g_im1(x, y) << " instead of " << c1;
                ASSERT_EQ(g_im2(x, y), c2) << "g(" << x << ", " << y << ")[2] = " << g_im2(x, y) << " instead of " << c2;
            }
        }
    }
}

#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(TupleReduction, AtomicUpdates) {
    // Test a tuple reduction on the gpu
    Target target = get_jit_target_from_environment();
    Func f;
    Var x, y, xo, yo, xi, yi;

    f(x, y) = Tuple(x + y, x - y);

    // Updates to a reduction are atomic.
    f(x, y) = Tuple(f(x, y)[1] * 2, f(x, y)[0] * 2);
    // now equals ((x - y)*2, (x + y)*2)

    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        f.update().gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon(y).vectorize(x, 32);
        f.update().hexagon(y).vectorize(x, 32);
    }

    Realization result = f.realize({1024, 1024});

    Buffer<int> a = result[0], b = result[1];

    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x - y) * 2;
            int correct_b = (x + y) * 2;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleReduction, AlternatingCpuGpu) {
    // Now test one that alternates between cpu and gpu per update step
    Target target = get_jit_target_from_environment();
    Func f;
    Var x, y, xo, yo, xi, yi;

    f(x, y) = Tuple(x + y, x - y);

    for (size_t i = 0; i < 10; i++) {
        // Swap the tuple elements and increment both
        f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
    }

    // Schedule the pure step and the odd update steps on the gpu
    if (target.has_gpu_feature()) {
        f.gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
    } else if (target.has_feature(Target::HVX)) {
        f.hexagon(y).vectorize(x, 32);
    }
    for (int i = 0; i < 10; i++) {
        f.update(i).unscheduled();
        if (i & 1) {
            if (target.has_gpu_feature()) {
                f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
            } else if (target.has_feature(Target::HVX)) {
                f.update(i).hexagon(y).vectorize(x, 32);
            }
        }
    }

    Realization result = f.realize({1024, 1024});

    Buffer<int> a = result[0], b = result[1];

    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x + y) + 10;
            int correct_b = (x - y) + 10;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleReduction, AlternatingCpuGpuReverse) {
    // Same as above, but switches which steps are gpu and cpu
    Target target = get_jit_target_from_environment();
    Func f;
    Var x, y, xo, yo, xi, yi;

    f(x, y) = Tuple(x + y, x - y);

    for (size_t i = 0; i < 10; i++) {
        // Swap the tuple elements and increment both
        f(x, y) = Tuple(f(x, y)[1] + 1, f(x, y)[0] + 1);
    }

    // Schedule the even update steps on the gpu
    for (int i = 0; i < 10; i++) {
        f.update(i).unscheduled();
        if (i & 1) {
            if (target.has_gpu_feature()) {
                f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
            } else if (target.has_feature(Target::HVX)) {
                f.update(i).hexagon(y).vectorize(x, 32);
            }
        }
    }

    Realization result = f.realize({1024, 1024});

    Buffer<int> a = result[0], b = result[1];

    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x + y) + 10;
            int correct_b = (x - y) + 10;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

TEST(TupleReduction, PartialBufferCopying) {
    // In this one, each step only uses one of the tuple elements
    // of the previous step, so only that buffer should get copied
    // back to host or copied to device.
    Target target = get_jit_target_from_environment();
    Func f;
    Var x, y, xo, yo, xi, yi;

    f(x, y) = Tuple(x + y - 1000, x - y + 1000);

    for (size_t i = 0; i < 10; i++) {
        f(x, y) = Tuple(f(x, y)[1] - 1, f(x, y)[1] + 1);
    }

    // Schedule the even update steps on the gpu
    for (int i = 0; i < 10; i++) {
        f.update(i).unscheduled();
        if ((i & 1) == 0) {
            if (target.has_gpu_feature()) {
                f.update(i).gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
            } else if (target.has_feature(Target::HVX)) {
                f.update(i).hexagon(y).vectorize(x, 32);
            }
        }
    }

    Realization result = f.realize({1024, 1024});

    Buffer<int> a = result[0], b = result[1];

    for (int y = 0; y < a.height(); y++) {
        for (int x = 0; x < a.width(); x++) {
            int correct_a = (x - y + 1000) + 8;
            int correct_b = (x - y + 1000) + 10;
            EXPECT_EQ(a(x, y), correct_a) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
            EXPECT_EQ(b(x, y), correct_b) << "result(" << x << ", " << y << ") = (" << a(x, y) << ", " << b(x, y) << ") instead of (" << correct_a << ", " << correct_b << ")";
        }
    }
}

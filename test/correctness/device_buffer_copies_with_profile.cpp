#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
void run_test(const Target &target) {
    // Sliding window with the producer on the GPU and the consumer on
    // the CPU. This requires a copy inside the loop over which we are
    // sliding. Currently this copies the entire buffer back and
    // forth, which is suboptimal in the general case. In this
    // specific case we're folded over y, so copying the entire buffer
    // is not much more than just copying the part that was modified.

    Func f0{"f0_on_cpu"}, f1{"f1_on_gpu"}, f2{"f2_on_cpu"};
    Var x, y, tx, ty;

    // Produce something on CPU
    f0(x, y) = x + y;
    f0.compute_root();

    // Which we use to produce something on GPU, causing a copy_to_device.
    f1(x, y) = f0(x, y) + f0(x, y + 1);
    f1.compute_root().gpu_tile(x, y, tx, ty, 8, 8);

    // Which in turn we use to produce something on CPU, causing a copy_to_host.
    f2(x, y) = f1(x, y) * 2;
    f2.compute_root();

    // Make the buffer a little bigger so we actually can see the copy time.
    Buffer<int> out = f2.realize({2000, 2000}, target);
    // Let's only verify a part of it...
    for (int yy = 0; yy < 100; yy++) {
        for (int xx = 0; xx < 100; xx++) {
            int correct = 4 * (xx + yy) + 2;
            EXPECT_EQ(out(xx, yy), correct) << "out(" << xx << ", " << yy << ")";
        }
    }
}

class DeviceBufferCopiesWithProfileTest : public ::testing::Test {
protected:
    Target target{get_jit_target_from_environment()};
    void SetUp() override {
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
    }
};
}  // namespace

TEST_F(DeviceBufferCopiesWithProfileTest, NoProfiler) {
    run_test(target);
}

TEST_F(DeviceBufferCopiesWithProfileTest, ThreadBasedProfiler) {
    run_test(target.with_feature(Target::Profile));
}

TEST_F(DeviceBufferCopiesWithProfileTest, TimerBasedProfiler) {
    if (target.os != Target::Linux) {
        GTEST_SKIP() << "Timer profiler test only runs on Linux.";
    }
    run_test(target.with_feature(Target::ProfileByTimer));
}

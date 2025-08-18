#include "Halide.h"
#include "halide_benchmark.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

// Return zero, slowly
Expr expensive_zero(Expr x, Expr y, Expr t, int n) {
    // Count how many Fermat's last theorem counterexamples we can find using n trials.
    RDom r(0, n);
    Func a, b, c;
    Var z;
    a(x, y, t, z) = random_int() % 1024 + 5;
    b(x, y, t, z) = random_int() % 1024 + 5;
    c(x, y, t, z) = random_int() % 1024 + 5;
    return sum(select(pow(a(x, y, t, r), 3) + pow(b(x, y, t, r), 3) == pow(c(x, y, t, r), 3), 1, 0));
}

class AsyncDeviceCopyTest : public ::testing::Test {
protected:
    const Target target{get_jit_target_from_environment()};

    Var x{"x"}, y{"y"}, t{"t"}, xo{"xo"}, yo{"yo"};
    Func avg{"avg"}, frames{"frames"};

    constexpr static int N = 16;
    RDom r{0, N};

    void SetUp() override {
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "WebAssembly does not support async() yet.";
        }
        frames(x, y, t) = expensive_zero(x, y, t, 1) + ((x + y) % 8) + t;
        avg(x, y) += frames(x, y, r);
    }

    void CheckResult() {
        Buffer<int> out = avg.realize({1024, 1024});

        for (int y = 0; y < out.height(); y++) {
            for (int x = 0; x < out.width(); x++) {
                int correct = ((x + y) % 8) * N + (N * (N - 1)) / 2;
                int actual = out(x, y);
                ASSERT_EQ(actual, correct) << "out(" << x << ", " << y << ") = " << actual << " instead of " << correct;
            }
        }

        // Report a benchmark, but don't assert anything about it. Not
        // sure how to tune the relative cost of the two stages to
        // make the async version reliably better than the non-async
        // version.
        const double time = Tools::benchmark(3, 3, [&]() {avg.realize(out); out.device_sync(); });
        std::cout << "avg.realize(out) took " << time << " s\n";
    }
};

}  // namespace

// Compute frames on GPU/CPU, and then sum then on CPU/GPU. async() lets
// us overlap the CPU computation with the copies.

TEST_F(AsyncDeviceCopyTest, SynchronousGpuToCpu) {
    // Synchronously GPU -> CPU
    avg.compute_root().update().reorder(x, y, r).vectorize(x, 8);
    frames.store_root().compute_at(frames.in(), Var::outermost()).gpu_tile(x, y, xo, yo, x, y, 16, 16);
    frames.in().store_root().compute_at(avg, r).copy_to_host();
    CheckResult();
}

TEST_F(AsyncDeviceCopyTest, AsynchronousGpuToCpu) {
    // Asynchronously GPU -> CPU, via a double-buffer
    avg.compute_root().update().reorder(x, y, r).vectorize(x, 8);
    frames.store_root().compute_at(frames.in(), Var::outermost()).gpu_tile(x, y, xo, yo, x, y, 16, 16);
    frames.in().store_root().compute_at(avg, r).copy_to_host().fold_storage(t, 2).async();
    CheckResult();
}

TEST_F(AsyncDeviceCopyTest, SynchronousCpuToGpu) {
    // Synchronously CPU -> GPU
    avg.compute_root().gpu_tile(x, y, xo, yo, x, y, 16, 16).update().reorder(x, y, r).gpu_tile(x, y, xo, yo, x, y, 16, 16);
    frames.store_root().compute_at(avg, r).vectorize(x, 8).fold_storage(t, 2).parallel(y);
    frames.in().store_root().compute_at(avg, r).copy_to_device();
    CheckResult();
}

TEST_F(AsyncDeviceCopyTest, AsynchronousCpuToGpu) {
    // Asynchronously CPU -> GPU, via a double-buffer
    avg.compute_root().gpu_tile(x, y, xo, yo, x, y, 16, 16).update().reorder(x, y, r).gpu_tile(x, y, xo, yo, x, y, 16, 16);
    frames.store_root().compute_at(avg, r).vectorize(x, 8).fold_storage(t, 2).async().parallel(y);
    frames.in().store_root().compute_at(avg, r).copy_to_device();
    CheckResult();
}

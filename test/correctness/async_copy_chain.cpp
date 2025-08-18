#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

class AsyncCopyChainTest : public ::testing::Test {
protected:
    const Target target{get_jit_target_from_environment()};

    Var x{"x"}, y{"y"};
    Func A{"A"}, B{"B"};

    void SetUp() override {
        if (target.arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly does not support async() yet.";
        }
        if (target.has_feature(Target::Vulkan) && (target.os == Target::Windows)) {
            GTEST_SKIP() << "Skipping test for Vulkan on Windows ... fails unless run on its own!";
        }
        A(x, y) = x + y;
        B(x, y) = A(x, y);
    }

    void Check() {
        Buffer<int> out = B.realize({256, 256});
        out.for_each_element([&](int x, int y) {
            ASSERT_EQ(out(x, y), x + y) << "out(" << x << ", " << y << ") = " << out(x, y) << " instead of " << (x + y);
        });
    }
};

class AsyncCopyChainGPUTest : public AsyncCopyChainTest {
protected:
    void SetUp() override {
        if (!target.has_gpu_feature()) {
            GTEST_SKIP() << "No GPU feature available";
        }
        AsyncCopyChainTest::SetUp();
    }
};

}  // namespace

// Make a list of extern pipeline stages (just copies) all async
// and connected by double buffers, then try various nestings of
// them. This is a stress test of the async extern storage folding
// logic.

TEST_F(AsyncCopyChainTest, BasicDoubleBuffered) {
    // Basic double-buffered A->B, with no extern stages
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    Check();
}

TEST_F(AsyncCopyChainTest, InjectCopyStage) {
    // Inject a copy stage between them
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();
    Check();
}

TEST_F(AsyncCopyChainTest, InjectCopyStageWithNesting) {
    // Inject a copy stage between them, but nest the first stage into it
    A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).async().copy_to_host();
    Check();
}

TEST_F(AsyncCopyChainTest, TwoCopyStagesFlat) {
    // Two copy stages, flat
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainTest, TwoCopyStagesNested) {
    // Two copy stages, each stage nested inside the outermost var of the next
    A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_host().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, TwoCopyStagesDeviceAndBackFlat) {
    // Two copy stages, to the device and back, flat
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_device().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, TwoCopyStagesDeviceAndBackNested) {
    // Two copy stages, to the device and back, each stage nested inside the outermost var of the next
    A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_device().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, SharedHostDevAllocationFlat) {
    // Make one of the copy stages non-extern to force a shared host-dev allocation
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, SharedHostDevAllocationNested) {
    A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_host().async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, MixedDeviceHostFlat) {
    A.store_root().compute_at(B, y).fold_storage(y, 2).async();
    A.in().store_root().compute_at(B, y).fold_storage(y, 2).copy_to_device().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).async();
    Check();
}

TEST_F(AsyncCopyChainGPUTest, MixedDeviceHostNested) {
    A.store_root().compute_at(A.in(), Var::outermost()).fold_storage(y, 2).async();
    A.in().store_root().compute_at(A.in().in(), Var::outermost()).fold_storage(y, 2).copy_to_device().async();
    A.in().in().store_root().compute_at(B, y).fold_storage(y, 2).async();
    Check();
}

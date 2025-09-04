#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

// TODO: move to error/

class InvalidGPULoopNestsTest : public ::testing::Test {
protected:
    Var v0, v1, v2, v3, v4, v5, v6, v7;
    Func f{"f"}, g{"g"};
    void SetUp() override {
        if (!exceptions_enabled()) {
            GTEST_SKIP() << "Halide was compiled without exceptions.";
        }
        if (!get_jit_target_from_environment().has_gpu_feature()) {
            GTEST_SKIP() << "No GPU target enabled.";
        }
    }
};

TEST_F(InvalidGPULoopNestsTest, ThreadsButNoBlocksOnOutputFunc) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_threads(v0);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, ThreadsButNoBlocksOnComputeRootFunc) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    f.compute_root().gpu_threads(v0);
    g.gpu_blocks(v1).gpu_threads(v0);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, TooManyBlocksLoops) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_blocks(v0, v1).gpu_blocks(v2, v3);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, TooManyThreadsLoops) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_threads(v0, v1).gpu_threads(v2, v3).gpu_blocks(v4);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, ThreadsOutsideOfBlocks) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_blocks(v0).gpu_threads(v1);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, NestedBlocksLoops) {
    // Something with a blocks loop compute_at inside something else with a blocks loop
    Var v0, v1, v2, v3, v4, v5, v6, v7;
    Func f{"f"}, g{"g"};
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_blocks(v0);
    f.compute_at(g, v0).gpu_blocks(v0);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, ComputeAtBetweenBlocksLoops) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_blocks(v0, v1);
    f.compute_at(g, v1);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, TooManyThreadsLoopsWithNesting) {
    // Something with too many threads loops once nesting is taken into account
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_threads(v0, v1).gpu_blocks(v2, v3);
    f.compute_at(g, v0).gpu_threads(v0, v1);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, TooManyThreadsLoopsInSpecialization) {
    // The same, but only in a specialization
    Var v0, v1, v2, v3, v4, v5, v6, v7;
    Param<bool> p;
    Func f{"f"}, g{"g"};
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_threads(v0, v1).gpu_blocks(v2, v3);
    f.compute_at(g, v0).gpu_threads(v0).specialize(p).gpu_threads(v1);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

TEST_F(InvalidGPULoopNestsTest, SerialLoopBetweenBlocksLoops) {
    f(v0, v1, v2, v3, v4, v5, v6, v7) = v0;
    g(v0, v1, v2, v3, v4, v5, v6, v7) = f(v0, v1, v2, v3, v4, v5, v6, v7);
    g.gpu_blocks(v5, v7);
    EXPECT_THROW(g.compile_jit(), Halide::CompileError);
}

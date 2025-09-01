#include "Halide.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

TEST(FuseGpuThreads, Basic) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        GTEST_SKIP() << "No GPU target enabled.";
    }

    // Canonical GPU for loop names are uniqued to make sure they don't collide
    // with user-provided names. We'll test that works by trying for a collision:
    unique_name("thread_id_x");
    unique_name("block_id_x");

    Var x("x"), y("y"), bx("bx"), by("by"), tx("tx"), ty("ty");

    Param<int> width("width"), height("height");
    ImageParam input(Int(32), 2, "input");

    Func tuple("tuple");
    tuple(x, y) = Tuple(input(x, y), input(x, y));

    Func consumer("consumer");
    consumer(x, y) = input(x, y) + tuple(x, y)[0];

    input.dim(0).set_bounds(0, width).dim(1).set_bounds(0, height).set_stride(width);

    // Schedule
    consumer.compute_root()
        .bound(x, 0, width)
        .bound(y, 0, height)
        .tile(x, y, bx, by, tx, ty, 64, 16, TailStrategy::ShiftInwards)
        .vectorize(tx, 4, TailStrategy::ShiftInwards)
        .gpu_blocks(bx, by)
        .gpu_threads(tx, ty);

    tuple.compute_at(consumer, bx)
        .vectorize(x, 4, TailStrategy::RoundUp)
        .gpu_threads(x, y);

    // Lower it and inspect the IR to verify the min/extent of GPU thread loops
    struct : IRMutator {
        using IRMutator::visit;
        std::vector<const For *> loops;
        Stmt visit(const For *op) override {
            if (op->for_type == ForType::GPUThread) {
                loops.emplace_back(op);
            }
            return IRMutator::visit(op);
        }
    } m;
    consumer.add_custom_lowering_pass(&m, nullptr);
    consumer.compile_jit();
    EXPECT_GT(m.loops.size(), 0) << "No loops found!";
    for (const For *loop : m.loops) {
        EXPECT_THAT(as_const_int(loop->min), testing::Optional(0));
        EXPECT_THAT(as_const_int(loop->extent), testing::Optional(16));
    }
}

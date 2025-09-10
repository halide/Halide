#include "Halide.h"
#include <gtest/gtest.h>

#include <utility>

using namespace Halide;
using namespace Halide::Internal;

namespace {
class CheckForStridedLoads final : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Load *op) override {
        if (const Ramp *r = op->index.as<Ramp>()) {
            if (op->name == buf_name) {
                bool dense = is_const_one(r->stride);
                found |= !dense;
                dense_loads += dense;
            }
        }
        return IRMutator::visit(op);
    }

public:
    bool found = false;
    int dense_loads = 0;
    std::string buf_name;

    explicit CheckForStridedLoads(std::string buf_name)
        : buf_name(std::move(buf_name)) {
    }
};

void check(Func f, int desired_dense_loads, std::string name = "buf") {
    CheckForStridedLoads checker(std::move(name));
    f.add_custom_lowering_pass(&checker, nullptr);
    f.compile_jit();
    EXPECT_FALSE(checker.found);
    EXPECT_EQ(checker.dense_loads, desired_dense_loads);
}

void check_not(Func f, int desired_dense_loads, std::string name = "buf") {
    CheckForStridedLoads checker(std::move(name));
    f.add_custom_lowering_pass(&checker, nullptr);
    f.compile_jit();
    EXPECT_TRUE(checker.found);
    EXPECT_EQ(checker.dense_loads, desired_dense_loads);
}

class StageStridedLoadsTest : public ::testing::Test {
protected:
    ImageParam buf{Float(32), 1, "buf"};
};
}  // namespace

TEST_F(StageStridedLoadsTest, ClusteredStridedLoads) {
    // Clusters of strided loads can share the same underlying dense load
    Func f;
    Var x;
    f(x) = buf(2 * x) + buf(2 * x + 1);
    f.vectorize(x, 8, TailStrategy::RoundUp);

    // We expect no strided loads, and one dense load
    check(f, 1);
}

TEST_F(StageStridedLoadsTest, ThreeTaps) {
    Func f;
    Var x;
    f(x) = buf(2 * x) + buf(2 * x + 1) + buf(2 * x + 2);
    f.vectorize(x, 8, TailStrategy::RoundUp);

    // We expect two dense loads in this case. One for the first two taps,
    // and one for the last tap.
    check(f, 2);
}

TEST_F(StageStridedLoadsTest, FourTaps) {
    // Check four taps
    Func f;
    Var x;
    f(x) = (buf(2 * x) + buf(2 * x + 2)) + (buf(2 * x + 1) + buf(2 * x + 3));
    f.vectorize(x, 8, TailStrategy::RoundUp);

    check(f, 2);
}

TEST_F(StageStridedLoadsTest, TupleLoads) {
    // Check tuples
    Func f;
    Var x;
    f(x) = {0.f, 0.f};
    f(x) += {buf(2 * x), buf(2 * x + 1)};
    f.update().vectorize(x, 8, TailStrategy::RoundUp);

    // In this case, the dense load appears twice across the two store
    // statements for the two tuple components, but it will get deduped by
    // llvm.
    check(f, 2);
}

TEST_F(StageStridedLoadsTest, FarApartConstantOffsets) {
    // Far apart constant offsets is still enough evidence that it's safe to
    // do a dense load.
    Func f;
    Var x;
    f(x) = buf(2 * x - 123) + buf(2 * x + 134);
    f.vectorize(x, 8, TailStrategy::RoundUp);

    check(f, 2);
}

TEST_F(StageStridedLoadsTest, LoadPartnersAcrossMultipleFuncs) {
    // Load partners can be split across multiple Funcs in the same block
    Func f, g;
    Var x;
    f(x) = buf(2 * x);
    g(x) = f(x) + buf(2 * x + 1);

    g.vectorize(x, 8, TailStrategy::RoundUp);
    f.compute_at(g, x).vectorize(x);

    check(g, 2);
}

TEST_F(StageStridedLoadsTest, LoadPartnersAcrossUpdateDefinitions) {
    // Load partners can be split across update definitions
    Func f, g;
    Var x;
    f(x) = buf(2 * x);
    f(x) += buf(2 * x + 1);
    g(x) = f(x);
    g.vectorize(x, 8, TailStrategy::RoundUp);

    check(g, 2);
}

TEST_F(StageStridedLoadsTest, LoadClustersAcrossUnrolledVariable) {
    // Load clusters can be split across an unrolled variable
    Func f, g;
    Var x, c;
    f(x, c) = buf(4 * x + c) + 4 * x;
    f.vectorize(x, 8, TailStrategy::RoundUp).bound(c, 0, 4).unroll(c).reorder(c, x);

    check(f, 4);
}

TEST_F(StageStridedLoadsTest, LoadClustersAcrossUnrelatedInnerLoops) {
    // Load clusters can even be split across unrelated inner loop nests
    // (provided they are known to have non-zero extent).

    Func f, g, h;
    Var c, x, y;
    g(x, y) = buf(2 * x) + y;
    h(x, y) = buf(2 * x + 1) + y;
    f(x, y, c) = g(x, y) + h(x, y) + c;

    Var xi, yi;
    f.tile(x, y, xi, yi, 8, 8, TailStrategy::RoundUp).vectorize(xi).reorder(c, x, y);
    g.compute_at(f, x).vectorize(x);
    h.compute_at(f, x).vectorize(x);
    check(f, 2);
}

TEST_F(StageStridedLoadsTest, DensifyInternalAllocations) {
    // We can always densify strided loads to internal allocations, because we
    // can just pad the allocation.
    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(2 * x);
    f.compute_at(g, x).vectorize(x);
    g.vectorize(x, 8, TailStrategy::RoundUp);
    check(g, 1, f.name());
}

TEST_F(StageStridedLoadsTest, DensifyUpToVectorSize) {
    // Strides up to the vector size are worth densifying. After that, it's better to just gather.
    Func f;
    Var x;
    f(x) = buf(15 * x) + buf(15 * x + 14);
    f.vectorize(x, 16, TailStrategy::RoundUp);

    check(f, 1);
}

TEST_F(StageStridedLoadsTest, GatherBeyondVectorSize) {
    Func f;
    Var x;
    f(x) = buf(16 * x) + buf(16 * x + 15);
    f.vectorize(x, 16, TailStrategy::RoundUp);

    check_not(f, 0);
}

TEST_F(StageStridedLoadsTest, ExternalAllocationShuffling) {
    // Strided loads to external allocations are handled by doing a weird-sized
    // dense load and then shuffling.
    Func f;
    Var x;
    f(x) = buf(3 * x);
    f.vectorize(x, 8, TailStrategy::RoundUp);
    check(f, 2);
}

TEST_F(StageStridedLoadsTest, ConditionalLoadsUseUnconditionalEvidence) {
    // Make a pair of unconditionally-executed loads, and check that a
    // conditionally-executed load can use it as evidence that a dense load in
    // one direction or the other is safe to do.
    Func f;
    Var x;
    f(x) = buf(2 * x) + buf(2 * x + 1);
    RDom r1(0, 1), r2(0, 1);
    Param<bool> p1, p2;
    r1.where(p1);
    r2.where(p2);
    f(x) += buf(2 * x + 3) + r1;
    f(x) += buf(2 * x - 3) + r2;

    Func g;
    g(x) = f(x);
    g.vectorize(x, 8, TailStrategy::RoundUp);
    f.compute_at(g, x).vectorize(x);
    f.update(0).vectorize(x);
    f.update(1).vectorize(x);

    check(g, 3);
}

TEST_F(StageStridedLoadsTest, NestedVectorization) {
    // Make a case that uses nested vectorization.
    Func f;
    Var x, c;

    f(c, x) = buf(2 * (2 * x + c)) + buf(2 * (2 * x + c) + 1);
    f.vectorize(x, 8, TailStrategy::RoundUp).bound(c, 0, 2).vectorize(c);
    f.output_buffer().dim(1).set_stride(2);
    check(f, 1);
}

TEST_F(StageStridedLoadsTest, VariousLoadSizesAndStrides) {
    // Do a variety of weird loads at weird sizes from an external buffer to
    // test the behavior that does two half-sized loads.
    Buffer<float> data(1024);
    for (int i = 0; i < 1024; i++) {
        data(i) = i;
    }
    buf.set(data);
    for (int size = 2; size <= 16; size += 2) {
        for (int stride = 2; stride <= 8; stride++) {
            Func f;
            Var x;
            f(x) = buf(stride * x);
            f.vectorize(x, size);

            Buffer<float> result = f.realize({1024 / stride});
            for (int i = 0; i < result.width(); i++) {
                EXPECT_EQ(result(i), data(stride * i))
                    << "stride = " << stride << ", size = " << size << ", i = " << i;
            }
        }
    }
}

#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;
using namespace Halide::Internal;

namespace {
struct CountForLoops final : IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        count++;
        return IRMutator::visit(op);
    }

    int count = 0;
};
}  // namespace

TEST(PartitionMaxFilterTest, PartitionMaxFilter) {
    // See https://github.com/halide/Halide/issues/5353

    const int width = 1280, height = 1024;
    Buffer<uint8_t> input(width, height);
    input.fill(0);

    Var x, y;

    Func clamped;
    clamped = BoundaryConditions::repeat_edge(input);

    Func max_x;
    max_x(x, y) = max(clamped(x - 1, y), clamped(x, y), clamped(x + 1, y));

    Func max_y;
    max_y(x, y) = max(max_x(x, y - 1), max_x(x, y), max_x(x, y + 1));

    CountForLoops counter;
    max_y.add_custom_lowering_pass(&counter, nullptr);

    Buffer<uint8_t> out;
    ASSERT_NO_THROW(out = max_y.realize({width, height}));

    // We expect a loop structure like:
    // Top of the image
    // for y:
    //  for x:
    // Middle of the image
    // for y:
    //  Left edge
    //  for x:
    //  Center
    //  for x:
    //  Right edge
    //  for x:
    // Bottom of the image
    // for y:
    //  for x:

    const int expected_loops = 8;
    EXPECT_EQ(counter.count, expected_loops)
        << "Loop was not partitioned into the expected number of cases";
}

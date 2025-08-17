#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestTupleOutputBoundsCheck() {
    // The code below used to not inject appropriate bounds checks.
    // See https://github.com/halide/Halide/issues/7343

    Var x;

    const int size = 1024;

    Func h;
    h(x) = {0, 0};
    RDom r(0, size);
    h(r) = {h(r - 100)[0], 0};

    Var xo, xi;
    h.split(x, xo, xi, 16, TailStrategy::RoundUp);
    h.update(0).unscheduled();

    Buffer<int> r0(size);
    Buffer<int> r1(size);
    h.realize({r0, r1});
}
}  // namespace

TEST(ErrorTests, TupleOutputBoundsCheck) {
    EXPECT_RUNTIME_ERROR(TestTupleOutputBoundsCheck, HasSubstr("TODO"));
}

#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;
using namespace Halide::Internal;

namespace {
void TestBadHostAlignment() {
    Func f;
    Var x, y;
    ImageParam in(UInt(8), 2);

    Buffer<uint8_t> param_buf(11, 10);
    param_buf.crop(0, 1, 10);

    in.set_host_alignment(512);
    f(x, y) = in(x, y);
    f.compute_root();

    in.set(param_buf);
    Buffer<uint8_t> result = f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadHostAlignment) {
    EXPECT_RUNTIME_ERROR(TestBadHostAlignment, MatchesPattern(R"(Input buffer p\d+ is accessed at 0, which is before the min \(1\) in dimension 0)"));
}

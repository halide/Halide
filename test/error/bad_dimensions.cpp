#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadDimensions() {
    ImageParam im(UInt(8), 2);

    Var x, y;
    Func f;

    f(x, y) = im(x, y);

    Buffer<uint8_t> b(10, 10, 3);
    im.set(b);

    f.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadDimensions) {
    EXPECT_RUNTIME_ERROR(
        TestBadDimensions,
        MatchesPattern(R"(Input buffer p\d+ requires a buffer of exactly )"
                       R"(2 dimensions, but the buffer passed in has )"
                       R"(3 dimensions)"));
}

#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadFold() {
    Var x, y, c;

    Func f, g;

    f(x, y) = x;
    g(x, y) = f(x - 1, y + 1) + f(x, y - 1);
    f.store_root().compute_at(g, y).fold_storage(y, 2);

    Buffer<int> im = g.realize({100, 1000});
}
}  // namespace

TEST(ErrorTests, BadFold) {
    EXPECT_RUNTIME_ERROR(
        TestBadFold,
        MatchesPattern(R"(The fold factor \(2\) of dimension v\d+ of )"
                       R"(f\d+ is too small to store the required region )"
                       R"(accessed by loop f\d+\.s\d+\.v\d+\.\$n \(3\)\.)"));
}

#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestAutodiffUnbounded() {
    Buffer<float> b(10);
    Func f("f"), g("g");
    Var x("x");
    Buffer<int> h(10);
    RDom r(h);

    f(x) = b(clamp(x, 0, 10));
    g() += f(h(r));
    Derivative d = propagate_adjoints(g);  // access to f is unbounded
}
}  // namespace

TEST(ErrorTests, AutodiffUnbounded) {
    EXPECT_COMPILE_ERROR(TestAutodiffUnbounded, MatchesPattern(R"(Access to function or buffer f(\$\d+)? at dimension 0 is not bounded\. We can only differentiate bounded accesses\.)"));
}

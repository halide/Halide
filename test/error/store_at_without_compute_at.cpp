#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestStoreAtWithoutComputeAt() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.store_at(h, y);

    h.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, StoreAtWithoutComputeAt) {
    EXPECT_COMPILE_ERROR(TestStoreAtWithoutComputeAt, MatchesPattern(R"(Func \"g(\$\d+)?\" is scheduled store_at\(\), but is inlined\. Funcs that use store_at must also call compute_at\.)"));
}

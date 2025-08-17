#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestHoistStorageWithoutComputeAt() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.hoist_storage(h, y);

    h.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, HoistStorageWithoutComputeAt) {
    EXPECT_COMPILE_ERROR(
        TestHoistStorageWithoutComputeAt,
        MatchesPattern(R"(Func \"g(\$\d+)?\" is scheduled hoist_storage\(\), but )"
                       R"(is inlined\. Funcs that use hoist_storage_root must also )"
                       R"(call compute_at\.)"));
}

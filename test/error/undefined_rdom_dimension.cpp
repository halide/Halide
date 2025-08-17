#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {

int error_occurred = false;
void my_error(JITUserContext *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

void TestUndefinedRdomDimension() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y"), c("c");

    RDom r(1, 99, "r");
    g(x, y, c) = 42;
    h(x, y, c) = 88;
    f(x, y, c) = g(x, y, c);
    f(r.x, r.y, c) = f(r.x - 1, r.y, c) + h(r.x, r.y, c);

    f.jit_handlers().custom_error = my_error;
    Buffer<int32_t> result = f.realize({100, 5, 3});

    assert(error_occurred);
}
}  // namespace

TEST(ErrorTests, UndefinedRdomDimension) {
    EXPECT_COMPILE_ERROR(TestUndefinedRdomDimension, MatchesPattern(R"(Use of undefined RDom dimension: r\$y)"));
}

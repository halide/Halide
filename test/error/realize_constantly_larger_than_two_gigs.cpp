#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {

int error_occurred = false;
void my_error(JITUserContext *ctx, const char *msg) {
    printf("Expected: %s\n", msg);
    error_occurred = true;
}

void TestRealizeConstantlyLargerThanTwoGigs() {
    Var x, y, z;
    RDom r(0, 4096, 0, 4096, 0, 256);
    Func big;
    big(x, y, z) = cast<uint8_t>(42);
    big.jit_handlers().custom_error = my_error;
    big.compute_root();

    Func grand_total;
    grand_total() = cast<uint8_t>(sum(big(r.x, r.y, r.z)));
    grand_total.jit_handlers().custom_error = my_error;

    Buffer<uint8_t> result = grand_total.realize();

    assert(error_occurred);
}
}  // namespace

TEST(ErrorTests, RealizeConstantlyLargerThanTwoGigs) {
    EXPECT_COMPILE_ERROR(TestRealizeConstantlyLargerThanTwoGigs, MatchesPattern(R"(Total size for allocation f\d+ is constant but exceeds 2\^31 - 1\.)"));
}

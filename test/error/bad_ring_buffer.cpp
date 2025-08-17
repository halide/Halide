#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadRingBuffer() {
    Func f("f"), g("g"), h("h");
    Var x("x"), y("y");

    f(x) = x;
    g(x) = f(x);
    h(x, y) = g(x);

    g.compute_at(h, y);

    // ring_buffer() requires an explicit hoist_storage().
    f.compute_root().ring_buffer(2);

    h.realize({10, 10});
}
}  // namespace

TEST(ErrorTests, BadRingBuffer) {
    EXPECT_COMPILE_ERROR(
        TestBadRingBuffer,
        MatchesPattern(R"(Func \"f(\$\d+)?\" is scheduled with )"
                       R"(ring_buffer\(\), but has matching store_at and )"
                       R"(hoist_storage levels\. Add an explicit )"
                       R"(hoist_storage directive to the schedule to fix )"
                       R"(the issue\.)"));
}

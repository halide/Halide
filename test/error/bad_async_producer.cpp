#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadAsyncProducer() {
    Func f{"f"}, g{"g"}, h{"h"};
    Var x;

    f(x) = cast<uint8_t>(x + 7);
    g(x) = f(x);
    h(x) = g(x);

    // The schedule below is an error. It should really be:
    // f.store_root().compute_at(g, Var::outermost());
    // So that it's nested inside the consumer h.
    f.store_root().compute_at(h, x);
    g.store_root().compute_at(h, x).async();

    Buffer<uint8_t> buf = h.realize({32});
}
}  // namespace

TEST(ErrorTests, BadAsyncProducer) {
    EXPECT_COMPILE_ERROR(
        TestBadAsyncProducer,
        MatchesPattern(R"(The Func f(\$\d+)? is consumed by async Func )"
                       R"(g(\$\d+)? and has a compute_at location in )"
                       R"(between the store_at location and the compute_at )"
                       R"(location of g(\$\d+)?\. This is only legal when )"
                       R"(f(\$\d+)? is both async and has a store_at location )"
                       R"(outside the store_at location of the consumer\.)"));
}

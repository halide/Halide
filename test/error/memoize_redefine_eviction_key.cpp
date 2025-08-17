#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestMemoizeRedefineEvictionKey() {
    Param<float> val;

    Func f, g;
    Var x, y;

    f(x, y) = val + cast<uint8_t>(x);
    g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);

    f.compute_root().memoize(EvictionKey(42));
    f.compute_root().memoize(EvictionKey(1764));

    val.set(23.0f);
    Buffer<float> out = g.realize({128, 128});
}
}  // namespace

TEST(ErrorTests, MemoizeRedefineEvictionKey) {
    EXPECT_COMPILE_ERROR(
        TestMemoizeRedefineEvictionKey,
        HasSubstr("Can't redefine memoize eviction key. First definition is: "
                  "(uint64)memoize_expr(reinterpret<uint64>((int64)42), 0) "
                  "new definition is: "
                  "(uint64)memoize_expr(reinterpret<uint64>((int64)1764), 0)"));
}

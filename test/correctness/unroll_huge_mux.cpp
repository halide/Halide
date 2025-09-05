#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

TEST(UnrollHugeMuxTest, Basic) {
#ifdef HALIDE_INTERNAL_USING_ASAN
    GTEST_SKIP() << "unroll_huge_mux requires set_compiler_stack_size() to work properly, which is disabled under ASAN.";
#endif

    Func f;
    Var x;

    std::vector<Expr> exprs;
    for (int i = 0; i < 5000; i++) {
        exprs.push_back(x & i);
    }

    f(x) = mux(x, exprs);

    f.bound(x, 0, (int)exprs.size());
    f.unroll(x);

    ASSERT_NO_THROW(f.compile_jit());
}

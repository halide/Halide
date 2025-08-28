#include "Halide.h"
#include "halide_test_error.h"

#include <gtest/gtest.h>
#include <iostream>

using namespace Halide;

// TODO: move to error/

namespace {
void expect_pure(const Func &f) {
    EXPECT_FALSE(f.has_update_definition()) << "Function unexpectedly has an update definition";
}
}  // namespace

TEST(ExceptionTest, CompileAndRuntimeErrors) {
#if HALIDE_WITH_EXCEPTIONS
    Func f1;
    Var x;

    // Bad because first arg is a float
    EXPECT_THROW({
        f1(x) = x + 3;
        f1(x / 3.0f) += 1; }, Halide::CompileError);
    expect_pure(f1);

    // Bad because f is not a Tuple
    EXPECT_THROW({ f1(x) += f1(x)[1]; }, Halide::CompileError);
    expect_pure(f1);

    // Bad because the RHS is the wrong type
    EXPECT_THROW({ f1(x) += 1.3f; }, Halide::CompileError);
    expect_pure(f1);

    // Bad because e is undefined
    EXPECT_THROW({
        Expr e;
        RDom r(0, 10);
        f1(r) = e; }, Halide::CompileError);
    expect_pure(f1);

    // Internal Errors
    EXPECT_THROW({
        Expr a;
        Expr b;
        Internal::Add::make(a, b); }, Halide::InternalError);

    EXPECT_THROW({ Internal::modulus_remainder(x > 3.0f); }, Halide::InternalError);

    // Runtime errors
    ImageParam im(Float(32), 1);
    Func f2;
    f2(x) = im(x) * 2.0f;
    EXPECT_THROW({ f2.realize({10}); }, Halide::RuntimeError);

    // Fix and try again
    Buffer<float> an_image(10);
    lambda(x, x * 7.0f).realize(an_image);
    im.set(an_image);
    Buffer<float> result = f2.realize({10});
    for (int i = 0; i < 10; i++) {
        float correct = i * 14.0f;
        EXPECT_EQ(result(i), correct) << "result(" << i << ")";
    }

    // Param range check
    EXPECT_THROW({
        Param<int> p;
        p.set_range(0, 10);
        p.set(-4);
        Func f4;
        f4(x) = p;
        f4.realize({10}); }, Halide::RuntimeError);
#else
    GTEST_SKIP() << "Halide was compiled without exceptions.";
#endif
}

#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// NB: You must compile with -rdynamic for llvm to be able to find the appropriate symbols
// This is not supported by the C backend.

// On windows, you need to use declspec to do the same.
int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL float c_function_my_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, c_function_my_func, int, float);

int call_counter2 = 0;
extern "C" HALIDE_EXPORT_SYMBOL float c_function_my_func2(int x, float y) {
    call_counter2++;
    return x * y;
}

int call_counter3 = 0;
extern "C" HALIDE_EXPORT_SYMBOL float c_function_my_func3(int x, float y) {
    call_counter3++;
    return x * y;
}
}  // namespace

TEST(CFunctionTest, Basic) {
    Var x, y;
    Func f;

    f(x, y) = c_function_my_func(x, cast<float>(y));

    Buffer<float> imf = f.realize({32, 32});

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            EXPECT_NEAR(imf(i, j), (float)(i * j), 0.001f);
        }
    }

    EXPECT_EQ(call_counter, 32 * 32)
        << "C function my_func was called " << call_counter << " times instead of " << 32 * 32;
}

TEST(CFunctionTest, SwitchJITExtern) {
    Var x, y;
    Func g;

    g(x, y) = c_function_my_func(x, cast<float>(y));

    Pipeline p(g);
    p.set_jit_externs({{"c_function_my_func", JITExtern{c_function_my_func2}}});
    Buffer<float> imf2 = p.realize({32, 32});

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            EXPECT_NEAR(imf2(i, j), (float)(i * j), 0.001f);
        }
    }

    EXPECT_EQ(call_counter2, 32 * 32)
        << "c_function_my_func2 was called " << call_counter2 << " times instead of " << 32 * 32;

    // Switch from my_func2 to my_func and verify a recompile happens.
    p.set_jit_externs({{"c_function_my_func", JITExtern{c_function_my_func3}}});
    Buffer<float> imf3 = p.realize({32, 32});

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            EXPECT_NEAR(imf3(i, j), (float)(i * j), 0.001f);
        }
    }

    EXPECT_EQ(call_counter3, 32 * 32)
        << "c_function_my_func3 was called " << call_counter3 << " times instead of " << 32 * 32;
}

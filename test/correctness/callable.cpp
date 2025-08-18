#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {

bool custom_malloc_called = false;
bool custom_free_called = false;

void *my_malloc(JITUserContext *user_context, size_t x) {
    custom_malloc_called = true;
    void *orig = malloc(x + 32);
    void *ptr = (void *)((((size_t)orig + 32) >> 5) << 5);
    ((void **)ptr)[-1] = orig;
    return ptr;
}

void my_free(JITUserContext *user_context, void *ptr) {
    custom_free_called = true;
    free(((void **)ptr)[-1]);
}

void *mischievous_malloc(JITUserContext *user_context, size_t x) {
    fprintf(stderr, "This should never get called\n");
    abort();
}

int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL float my_extern_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, my_extern_func, int, float);

}  // namespace

TEST(CallableTest, DefaultConstructor) {
    // Check that we can default-construct a Callable.
    Callable c;
    ASSERT_FALSE(c.defined());

    // This will assert-fail.
    // c(0,1,2);
}

TEST(CallableTest, Basic) {
    Param<int32_t> p_int(42);
    Param<float> p_float(1.0f);
    ImageParam p_img(UInt(8), 2);

    Var x("x"), y("y");
    Func f("f");

    f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

    Buffer<uint8_t> in1(10, 10);
    Buffer<uint8_t> in2(10, 10);

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            in1(i, j) = i + j * 10;
            in2(i, j) = i * 10 + j;
        }
    }

    Callable c = f.compile_to_callable({p_img, p_int, p_float});

    {
        Buffer<uint8_t> out1(10, 10);
        ASSERT_EQ(c(in1, 42, 1.0f, out1), 0);

        Buffer<uint8_t> out2(10, 10);
        ASSERT_EQ(c(in2, 22, 2.0f, out2), 0);

        Buffer<uint8_t> out3(10, 10);
        ASSERT_EQ(c(in1, 12, 1.0f, out3), 0);

        Buffer<uint8_t> out4(10, 10);
        ASSERT_EQ(c(in2, 16, 1.0f, out4), 0);

        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                ASSERT_EQ(out1(i, j), i + j * 10 + 42);
                ASSERT_EQ(out2(i, j), i * 10 + j + 11);
                ASSERT_EQ(out3(i, j), i + j * 10 + 12);
                ASSERT_EQ(out4(i, j), i * 10 + j + 16);
            }
        }
    }

    {
        // Test bounds inference
        Buffer<uint8_t> in_bounds(nullptr, 1, 1);
        Buffer<uint8_t> out_bounds(nullptr, 20, 20);

        ASSERT_EQ(c(in_bounds, 42, 1.0f, out_bounds), 0);

        ASSERT_TRUE(in_bounds.defined());
        ASSERT_EQ(in_bounds.dim(0).extent(), 20);
        ASSERT_EQ(in_bounds.dim(1).extent(), 20);
        ASSERT_EQ(in1.dim(0).extent(), 10);
        ASSERT_EQ(in1.dim(1).extent(), 10);
    }
}

TEST(CallableTest, CallableOverrideAlloc) {
    // Override Halide's malloc and free (except under wasm),
    // make sure that Callable freezes the values
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "Callables aren't supported in WebAssembly";
    }

    custom_malloc_called = false;
    custom_free_called = false;

    Func f, g;
    Var x;

    f(x) = x;
    g(x) = f(x);
    f.compute_root();

    g.jit_handlers().custom_malloc = my_malloc;
    g.jit_handlers().custom_free = my_free;

    Callable c = g.compile_to_callable({});

    // Changing g's handlers shouldn't affect any existing Callables
    g.jit_handlers().custom_malloc = mischievous_malloc;

    Buffer<int> im(100000);
    ASSERT_EQ(c(im), 0);

    ASSERT_TRUE(custom_malloc_called);
    ASSERT_TRUE(custom_free_called);
}

TEST(CallableTest, ParamVoidPointer) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "Callables aren't supported in WebAssembly";
    }
    Func f("f"), g("g");
    Var x("x");
    Param<void *> handle("handle");

    f(x) = reinterpret<uint64_t>(handle);

    g(x) = reinterpret<uint64_t>(handle);
    g.vectorize(x, 4);

    Callable cf = f.compile_to_callable({handle});
    Callable cg = g.compile_to_callable({handle});

    int foo = 0;

    Buffer<uint64_t> out1(4);
    // Create a dummy JITUserContext here just to test that
    // passing one explicitly works correctly.
    JITUserContext empty;
    ASSERT_EQ(cf(&empty, &foo, out1), 0);

    Buffer<uint64_t> out2(4);
    ASSERT_EQ(cg(&foo, out2), 0);

    uint64_t correct = (uint64_t)((uintptr_t)(&foo));

    for (int x = 0; x < out1.width(); x++) {
        ASSERT_EQ(out1(x), correct);
        ASSERT_EQ(out2(x), correct);
    }
}

TEST(CallableTest, JITExterns) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        GTEST_SKIP() << "Callables aren't supported in WebAssembly";
    }
    call_counter = 0;

    std::vector<ExternFuncArgument> args;
    args.push_back(user_context_value());

    Var x, y;
    Func monitor;
    monitor(x, y) = my_extern_func(x, cast<float>(y));

    Func f;
    f.define_extern("extern_func", args, Float(32), 2);

    Pipeline p(f);
    p.set_jit_externs({{"extern_func", JITExtern{monitor}}});

    Callable c = p.compile_to_callable({});

    // Changing g's jit_externs shouldn't affect any existing Callables
    p.set_jit_externs({});

    Buffer<float> imf(32, 32);
    ASSERT_EQ(c(imf), 0);

    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            float correct = (float)(i * j);
            ASSERT_NEAR(imf(i, j), correct, 0.001f);
        }
    }

    ASSERT_EQ(call_counter, 32 * 32);
}

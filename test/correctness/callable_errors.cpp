#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;

namespace {

std::string error_msg;
void my_error(JITUserContext *ucon, const char *msg) {
    error_msg = msg;
}

void expect_failure(int r, const char *expected_msg) {
    if (r == 0) {
        std::cerr << "Expected failure, got success\n";
        exit(1);
    }
    if (!strstr(error_msg.c_str(), expected_msg)) {
        std::cerr << "Expected error containing (" << expected_msg << "), but got (" << error_msg << ")\n";
        exit(1);
    }
    std::cout << "Saw expected: (" << expected_msg << ")\n";
    error_msg = "";
}

void expect_success(int r) {
    if (r != 0) {
        std::cerr << "Expected success, got failure\n";
        exit(1);
    }
    if (!error_msg.empty()) {
        std::cerr << "Expected NO ERROR, got (" << error_msg << ")\n";
        exit(1);
    }
    std::cout << "Saw expected: (NO ERROR)\n";
    error_msg = "";
}

void test_bad_untyped_calls() {
    // Test custom error handler in the JITHandler
    {
        Param<int32_t> p_int("p_int");
        Param<float> p_float("p_float");
        ImageParam p_img(UInt(8), 2, "p_img");

        Var x("x"), y("y");
        Func f("fn1");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        f.jit_handlers().custom_error = my_error;

        Callable c = f.compile_to_callable({p_img, p_int, p_float});

        Buffer<uint8_t> in1(10, 10), result1(10, 10);
        in1.fill(0);

        expect_success(c(in1, 2, 1.0f, result1));
        expect_failure(c((const halide_buffer_t *)nullptr, 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c((halide_buffer_t *)nullptr, 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<const uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<const uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<const void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<const void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(Buffer<void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(42, 2, 1.0f, result1), "Argument 1 of 4 ('p_img') was expected to be a buffer of type 'uint8' and dimension 2");
        expect_failure(c(in1, 2.25, 1.0f, result1), "Argument 2 of 4 ('p_int') was expected to be a scalar of type 'int32' and dimension 0");
        expect_failure(c(in1, 2, 1, result1), "Argument 3 of 4 ('p_float') was expected to be a scalar of type 'float32' and dimension 0");
        expect_failure(c(in1, 2, 1.0f, (const halide_buffer_t *)nullptr), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, (halide_buffer_t *)nullptr), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<const uint8_t, 2>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<const uint8_t, AnyDims>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<const void, 2>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<const void, AnyDims>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<uint8_t, 2>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<uint8_t, AnyDims>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<void, 2>()), "Buffer argument fn1 is nullptr");
        expect_failure(c(in1, 2, 1.0f, Buffer<void, AnyDims>()), "Buffer argument fn1 is nullptr");
    }

    // Test custom error handler in the JITUserContext
    {
        Param<int32_t> p_int("p_int");
        Param<float> p_float("p_float");
        ImageParam p_img(UInt(8), 2, "p_img");

        Var x("x"), y("y");
        Func f("fn2");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        Callable c = f.compile_to_callable({p_img, p_int, p_float});

        Buffer<uint8_t> in1(10, 10), result1(10, 10);
        in1.fill(0);

        JITUserContext context;
        context.handlers.custom_error = my_error;

        expect_success(c(&context, in1, 2, 1.0f, result1));
        expect_failure(c(&context, (const halide_buffer_t *)nullptr, 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, (halide_buffer_t *)nullptr, 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<const uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<const uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<const void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<const void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, Buffer<void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c(&context, 42, 2, 1.0f, result1), "Argument 1 of 4 ('p_img') was expected to be a buffer of type 'uint8' and dimension 2");
        expect_failure(c(&context, in1, 2.25, 1.0f, result1), "Argument 2 of 4 ('p_int') was expected to be a scalar of type 'int32' and dimension 0");
        expect_failure(c(&context, in1, 2, 1, result1), "Argument 3 of 4 ('p_float') was expected to be a scalar of type 'float32' and dimension 0");
        expect_failure(c(&context, in1, 2, 1.0f, (const halide_buffer_t *)nullptr), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, (halide_buffer_t *)nullptr), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<const uint8_t, 2>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<const uint8_t, AnyDims>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<const void, 2>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<const void, AnyDims>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<uint8_t, 2>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<uint8_t, AnyDims>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<void, 2>()), "Buffer argument fn2 is nullptr");
        expect_failure(c(&context, in1, 2, 1.0f, Buffer<void, AnyDims>()), "Buffer argument fn2 is nullptr");
    }
}

void test_bad_typed_calls() {
    // Test custom error handler in the JITHandler
    {
        Param<int32_t> p_int("p_int");
        Param<float> p_float("p_float");
        ImageParam p_img(UInt(8), 2, "p_img");

        Var x("x"), y("y");
        Func f("fn3");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        f.jit_handlers().custom_error = my_error;

        Callable c = f.compile_to_callable({p_img, p_int, p_float});

        Buffer<uint8_t> in1(10, 10), result1(10, 10);
        in1.fill(0);

        auto c_typed = c.make_std_function<Buffer<uint8_t, 2>, int32_t, float, Buffer<uint8_t, 2>>();
        expect_success(c_typed(in1, 2, 1.0f, result1));

        // make_std_function succeeds, but calls to it fail at runtime
        expect_failure(c_typed(Buffer<uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(Buffer<uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(Buffer<void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(Buffer<void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(in1, 2, 1.0f, Buffer<uint8_t, 2>()), "Buffer argument fn3 is nullptr");
        expect_failure(c_typed(in1, 2, 1.0f, Buffer<uint8_t, AnyDims>()), "Buffer argument fn3 is nullptr");
        expect_failure(c_typed(in1, 2, 1.0f, Buffer<void, 2>()), "Buffer argument fn3 is nullptr");
        expect_failure(c_typed(in1, 2, 1.0f, Buffer<void, AnyDims>()), "Buffer argument fn3 is nullptr");

        // Calls to make_std_function fail
        c.make_std_function<bool, int32_t, float, Buffer<uint8_t, 2>>();
        expect_failure(-1, "Argument 1 of 4 ('p_img') was expected to be a buffer of type 'uint8' and dimension 2");

        c.make_std_function<Buffer<uint8_t, 2>, bool, float, Buffer<uint8_t, 2>>();
        expect_failure(-1, "Argument 2 of 4 ('p_int') was expected to be a scalar of type 'int32' and dimension 0");

        c.make_std_function<Buffer<uint8_t, 2>, int32_t, bool, Buffer<uint8_t, 2>>();
        expect_failure(-1, "Argument 3 of 4 ('p_float') was expected to be a scalar of type 'float32' and dimension 0");

        c.make_std_function<Buffer<uint8_t, 2>, int32_t, float, bool>();
        expect_failure(-1, "Argument 4 of 4 ('fn3') was expected to be a buffer of type 'uint8' and dimension 2");
    }

    // Test custom error handler in the JITUserContext
    {
        Param<int32_t> p_int("p_int");
        Param<float> p_float("p_float");
        ImageParam p_img(UInt(8), 2, "p_img");

        Var x("x"), y("y");
        Func f("fn4");

        f(x, y) = p_img(x, y) + cast<uint8_t>(p_int / p_float);

        Callable c = f.compile_to_callable({p_img, p_int, p_float});

        Buffer<uint8_t> in1(10, 10), result1(10, 10);
        in1.fill(0);

        JITUserContext context;
        context.handlers.custom_error = my_error;

        auto c_typed = c.make_std_function<JITUserContext *, Buffer<uint8_t, 2>, int32_t, float, Buffer<uint8_t, 2>>();
        expect_success(c_typed(&context, in1, 2, 1.0f, result1));

        // make_std_function succeeds, but calls to it fail at runtime
        expect_failure(c_typed(&context, Buffer<uint8_t, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(&context, Buffer<uint8_t, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(&context, Buffer<void, 2>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(&context, Buffer<void, AnyDims>(), 2, 1.0f, result1), "Buffer argument p_img is nullptr");
        expect_failure(c_typed(&context, in1, 2, 1.0f, Buffer<uint8_t, 2>()), "Buffer argument fn4 is nullptr");
        expect_failure(c_typed(&context, in1, 2, 1.0f, Buffer<uint8_t, AnyDims>()), "Buffer argument fn4 is nullptr");
        expect_failure(c_typed(&context, in1, 2, 1.0f, Buffer<void, 2>()), "Buffer argument fn4 is nullptr");
        expect_failure(c_typed(&context, in1, 2, 1.0f, Buffer<void, AnyDims>()), "Buffer argument fn4 is nullptr");

        // Note that since make_std_function doesn't take a JITUserContext, we aren't able to hook the error handler
        // here, so all of these will just assert-fail and kill the test. We'll just skip the tests here, as it's
        // exercised elsewhere enough.
    }
}
}  // namespace

int main(int argc, char **argv) {
    test_bad_untyped_calls();
    test_bad_typed_calls();

    printf("Success!\n");
}

#include <assert.h>
#include <stdio.h>
#include <Halide.h>

using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int call_count = 0;

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" DLLEXPORT int count_calls(buffer_t *out) {
    if (out->host) {
        call_count++;
        for (int32_t i = 0; i < out->extent[0]; i++) {
            for (int32_t j = 0; j < out->extent[1]; j++) {
                out->host[i * out->stride[0] + j * out->stride[1]] = 42;
            }
        }
    }
    return 0;
}

int call_count_with_arg = 0;

// Imagine that this loads from a file, or tiled storage. Here we'll just fill in the data using sinf.
extern "C" DLLEXPORT int count_calls_with_arg(uint8_t val, buffer_t *out) {
    if (out->host) {
        call_count_with_arg++;
        for (int32_t i = 0; i < out->extent[0]; i++) {
            for (int32_t j = 0; j < out->extent[1]; j++) {
                out->host[i * out->stride[0] + j * out->stride[1]] = val;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    {
        Func count_calls;
        count_calls.define_extern("count_calls",
                                  std::vector<ExternFuncArgument>(),
                                  UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls(x, y) + count_calls(x, y);
        count_calls.compute_cached();

        f.compile_to_lowered_stmt("/tmp/compute_cached.stmt");

        Image<uint8_t> out1 = f.realize(256, 256);
        Image<uint8_t> out2 = f.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == (42 + 42));
                assert(out2(i, j) == (42 + 42));
            }
        }
        assert(call_count == 1);
    }

    call_count = 0;

    {
        Func count_calls_23;
        count_calls_23.define_extern("count_calls_with_arg",
                                     Internal::vec(ExternFuncArgument(cast<uint8_t>(23))),
                                     UInt(8), 2);

        Func count_calls_42;
        count_calls_42.define_extern("count_calls_with_arg",
                                     Internal::vec(ExternFuncArgument(cast<uint8_t>(42))),
                                     UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls_23(x, y) + count_calls_42(x, y);
        count_calls_23.compute_cached();
        count_calls_42.compute_cached();

        f.compile_to_lowered_stmt("/tmp/compute_cached_with_arg.stmt");

        Image<uint8_t> out1 = f.realize(256, 256);
        Image<uint8_t> out2 = f.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == (23 + 42));
                assert(out2(i, j) == (23 + 42));
            }
        }
        assert(call_count_with_arg == 2);
    }

    {
        Param<uint8_t> val1;
        Param<uint8_t> val2;

        call_count_with_arg = 0;
        Func count_calls_val1;
        count_calls_val1.define_extern("count_calls_with_arg",
                                       Internal::vec(ExternFuncArgument(Expr(val1))),
                                       UInt(8), 2);

        Func count_calls_val2;
        count_calls_val2.define_extern("count_calls_with_arg",
                                       Internal::vec(ExternFuncArgument(Expr(val2))),
                                       UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls_val1(x, y) + count_calls_val2(x, y);
        count_calls_val1.compute_cached();
        count_calls_val2.compute_cached();

        f.compile_to_lowered_stmt("/tmp/compute_cached_params.stmt");

        val1.set(23);
        val2.set(42);

        Image<uint8_t> out1 = f.realize(256, 256);
        Image<uint8_t> out2 = f.realize(256, 256);

        val1.set(42);
        Image<uint8_t> out3 = f.realize(256, 256);

        val1.set(23);
        Image<uint8_t> out4 = f.realize(256, 256);

        val1.set(42);
        Image<uint8_t> out5 = f.realize(256, 256);

        val2.set(57);
        Image<uint8_t> out6 = f.realize(256, 256);


        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == (23 + 42));
                assert(out2(i, j) == (23 + 42));
                assert(out3(i, j) == (42 + 42));
                assert(out4(i, j) == (23 + 42));
                assert(out5(i, j) == (42 + 42));
                assert(out6(i, j) == (42 + 57));
            }
        }
        assert(call_count_with_arg == 4);
    }

    printf("Success!\n");
    return 0;
}

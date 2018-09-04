#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "Halide.h"
#include "HalideRuntime.h"

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

// External functions to track whether the cache is working.

int call_count = 0;

extern "C" DLLEXPORT int count_calls(halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(42);
    }
    return 0;
}

int call_count_with_arg = 0;

extern "C" DLLEXPORT int count_calls_with_arg(uint8_t val, halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count_with_arg++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(val);
    }
    return 0;
}

int call_count_with_arg_parallel[8];

extern "C" DLLEXPORT int count_calls_with_arg_parallel(uint8_t val, halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count_with_arg_parallel[out->dim[2].min]++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(val);
    }
    return 0;
}

int call_count_staged[4];

extern "C" DLLEXPORT int count_calls_staged(int32_t stage, uint8_t val, halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        for (int i = 0; i < out->dimensions; i++) {
            in->dim[i] = out->dim[i];
        }
    } else if (!out->is_bounds_query()) {
        assert(stage < static_cast<int32_t>(sizeof(call_count_staged)/sizeof(call_count_staged[0])));
        call_count_staged[stage]++;
        Halide::Runtime::Buffer<uint8_t> out_buf(*out), in_buf(*in);
        out_buf.for_each_value([&](uint8_t &out, uint8_t &in) {out = in + val;}, in_buf);
    }
    return 0;
}

void simple_free(void *user_context, void *ptr) {
    free(ptr);
}

void *flakey_malloc(void * /* user_context */, size_t x) {
    if ((rand() % 4) == 0) {
        return nullptr;
    } else {
        return malloc(x);
    }
}

bool error_occured = false;
void record_error(void *user_context, const char *msg) {
    error_occured = true;
}

int main(int argc, char **argv) {

    {
        call_count = 0;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        Func f, f_memoized;
        f_memoized() = count_calls(0, 0);
        f_memoized.compute_root().memoize();
        f() = f_memoized() + 1;
        f_memoized.compute_root().memoize();

        Buffer<uint8_t> result1 = f.realize();
        Buffer<uint8_t> result2 = f.realize();

        assert(result1(0) == 43);
        assert(result2(0) == 43);

        assert(call_count == 1);
    }

    {
        call_count = 0;
        Param<int32_t> coord;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        Func f, g;
        Var x, y;
        f() = count_calls(coord, coord);
        f.compute_root().memoize();

        g(x, y) = f();

        coord.set(0);
        Buffer<uint8_t> out1 = g.realize(256, 256);
        Buffer<uint8_t> out2 = g.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == 42);
                assert(out2(i, j) == 42);
            }
        }
        assert(call_count == 1);

        coord.set(1);
        Buffer<uint8_t> out3 = g.realize(256, 256);
        Buffer<uint8_t> out4 = g.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out3(i, j) == 42);
                assert(out4(i, j) == 42);
            }
        }
        assert(call_count == 2);
    }

    {
        call_count = 0;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls(x, y) + count_calls(x, y);
        count_calls.compute_root().memoize();

        Buffer<uint8_t> out1 = f.realize(256, 256);
        Buffer<uint8_t> out2 = f.realize(256, 256);

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
        count_calls_23.define_extern("count_calls_with_arg", {cast<uint8_t>(23)}, UInt(8), 2);

        Func count_calls_42;
        count_calls_42.define_extern("count_calls_with_arg", {cast<uint8_t>(42)}, UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls_23(x, y) + count_calls_42(x, y);
        count_calls_23.compute_root().memoize();
        count_calls_42.compute_root().memoize();

        Buffer<uint8_t> out1 = f.realize(256, 256);
        Buffer<uint8_t> out2 = f.realize(256, 256);

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
        count_calls_val1.define_extern("count_calls_with_arg", {val1}, UInt(8), 2);

        Func count_calls_val2;
        count_calls_val2.define_extern("count_calls_with_arg", {val2}, UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls_val1(x, y) + count_calls_val2(x, y);
        count_calls_val1.compute_root().memoize();
        count_calls_val2.compute_root().memoize();

        val1.set(23);
        val2.set(42);

        Buffer<uint8_t> out1 = f.realize(256, 256);
        Buffer<uint8_t> out2 = f.realize(256, 256);

        val1.set(42);
        Buffer<uint8_t> out3 = f.realize(256, 256);

        val1.set(23);
        Buffer<uint8_t> out4 = f.realize(256, 256);

        val1.set(42);
        Buffer<uint8_t> out5 = f.realize(256, 256);

        val2.set(57);
        Buffer<uint8_t> out6 = f.realize(256, 256);


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

    {
        Param<float> val;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls(x, y) + count_calls(x, y);
        count_calls.compute_root().memoize();

        val.set(23.0f);
        Buffer<uint8_t> out1 = f.realize(256, 256);
        val.set(23.4f);
        Buffer<uint8_t> out2 = f.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == (23 + 23));
                assert(out2(i, j) == (23 + 23));
            }
        }
        assert(call_count_with_arg == 2);
    }

    {
        Param<float> val;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {memoize_tag(cast<uint8_t>(val))}, UInt(8), 2);

        Func f;
        Var x, y;
        f(x, y) = count_calls(x, y) + count_calls(x, y);
        count_calls.compute_root().memoize();

        val.set(23.0f);
        Buffer<uint8_t> out1 = f.realize(256, 256);
        val.set(23.4f);
        Buffer<uint8_t> out2 = f.realize(256, 256);

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == (23 + 23));
                assert(out2(i, j) == (23 + 23));
            }
        }
        assert(call_count_with_arg == 1);
    }

    {
        // Case with bounds computed not equal to bounds realized.
        Param<float> val;
        Param<int32_t> index;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);
        Func f, g, h;
        Var x;

        f(x) = count_calls(x, 0) + cast<uint8_t>(x);
        g(x) = f(x) + 1;
        h(x) = g(4) + g(index);

        f.compute_root().memoize();
        g.vectorize(x, 8).compute_at(h, x);

        val.set(23.0f);
        index.set(2);
        Buffer<uint8_t> out1 = h.realize(1);

        assert(out1(0) == (uint8_t)(2 * (23 + 1) + 4 + 2));
        assert(call_count_with_arg == 3);

        index.set(4);
        out1 = h.realize(1);

        assert(out1(0) == (uint8_t)(2 * (23 + 1) + 4 + 4));
        assert(call_count_with_arg == 4);
    }

    /*{
        // Test Tuple case
        Param<float> val;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);

        Func f;
        Var x, y, xi, yi;
        f(x, y) = Tuple(count_calls(x, y) + cast<uint8_t>(x), x);
        count_calls.compute_root().memoize();
        f.compute_root().memoize();

        Func g;
        g(x, y) = Tuple(f(x, y)[0] + f(x - 1, y)[0] + f(x + 1, y)[0], f(x, y)[1]);

        val.set(23.0f);
        Realization out = g.realize(128, 128);
        Buffer<uint8_t> out0 = out[0];
        Buffer<int32_t> out1 = out[1];


        for (int32_t i = 0; i < 100; i++) {
            for (int32_t j = 0; j < 100; j++) {
                assert(out0(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
                assert(out1(i, j) == i);
            }
        }
        out = g.realize(128, 128);
        out0 = out[0];
        out1 = out[1];


        for (int32_t i = 0; i < 100; i++) {
            for (int32_t j = 0; j < 100; j++) {
                assert(out0(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
                assert(out1(i, j) == i);
            }
        }
        assert(call_count_with_arg == 1);
    }*/

    {
        // Test cache eviction
        Param<float> val;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);

        Func f;
        Var x, y, xi, yi;
        f(x, y) = count_calls(x, y) + cast<uint8_t>(x);
        count_calls.compute_root().memoize();

        Func g;
        g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);
        Internal::JITSharedRuntime::memoization_cache_set_size(1000000);

        for (int v = 0; v < 1000; v++) {
            int r = rand() % 256;
            val.set((float)r);
            Buffer<uint8_t> out1 = g.realize(128, 128);

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }
        // TODO work out an assertion on call count here.
        fprintf(stderr, "Call count is %d.\n", call_count_with_arg);

        // Return cache size to default.
        Internal::JITSharedRuntime::memoization_cache_set_size(0);
    }

    {
        // Test flushing entire cache with a single element larger than the cache
        Param<float> val;

        call_count_with_arg = 0;
        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);

        Func f;
        Var x, y, xi, yi;
        f(x, y) = count_calls(x, y) + cast<uint8_t>(x);
        count_calls.compute_root().memoize();

        Func g;
        g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);
        Internal::JITSharedRuntime::memoization_cache_set_size(1000000);

        for (int v = 0; v < 1000; v++) {
            int r = rand() % 256;
            val.set((float)r);
            Buffer<uint8_t> out1 = g.realize(128, 128);

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }

        // TODO work out an assertion on call count here.
        fprintf(stderr, "Call count before oversize realize is %d.\n", call_count_with_arg);
        call_count_with_arg = 0;

        Buffer<uint8_t> big = g.realize(1024, 1024);
        Buffer<uint8_t> big2 = g.realize(1024, 1024);

        // TODO work out an assertion on call count here.
        fprintf(stderr, "Call count after oversize realize is %d.\n", call_count_with_arg);

        call_count_with_arg = 0;
        for (int v = 0; v < 1000; v++) {
            int r = rand() % 256;
            val.set((float)r);
            Buffer<uint8_t> out1 = g.realize(128, 128);

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }

        fprintf(stderr, "Call count is %d.\n", call_count_with_arg);

        // Return cache size to default.
        Internal::JITSharedRuntime::memoization_cache_set_size(0);
    }

    {
        // Test parallel cache access
        Param<float> val;

        Func count_calls;
        count_calls.define_extern("count_calls_with_arg_parallel", {cast<uint8_t>(val)}, UInt(8), 3);

        Func f;
        Var x, y;
        // Ensure that all calls map to the same cache key, but pass a thread ID
        // through to avoid having to do locking or an atomic add
        f(x, y) = count_calls(x, y % 4, memoize_tag(y / 16, 0)) + cast<uint8_t>(x);

        Func g;
        g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);
        count_calls.compute_at(f, y).memoize();
        f.compute_at(g, y).memoize();
        g.parallel(y, 16);

        val.set(23.0f);
        Internal::JITSharedRuntime::memoization_cache_set_size(1000000);
        Buffer<uint8_t> out = g.realize(128, 128);

        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
                assert(out(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
            }
        }

        // TODO work out an assertion on call counts here.
        for (int i = 0; i < 8; i++) {
          fprintf(stderr, "Call count for thread %d is %d.\n", i, call_count_with_arg_parallel[i]);
        }

        // Return cache size to default.
        Internal::JITSharedRuntime::memoization_cache_set_size(0);
    }

    {
        Param<float> val;

        Func f;
        Var x, y;
        f(x, y) = cast<uint8_t>((x << 8) + y);

        Func prev_func = f;

        Func stage[4];
        for (int i = 0; i < 4; i++) {
            std::vector<ExternFuncArgument> args(3);
            args[0] = cast<int32_t>(i);
            args[1] = cast<int32_t>(val);
            args[2] = prev_func;
            stage[i].define_extern("count_calls_staged",
                                   args,
                                   UInt(8), 2);
            prev_func = stage[i];
        }

        f.compute_root();
        for (int i = 0; i < 3; i++) {
          stage[i].compute_root();
        }
        stage[3].compute_root().memoize();
        Func output;
        output(_) = stage[3](_) + 1;
        val.set(23.0f);
        Buffer<uint8_t> result = output.realize(128, 128);

        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
              assert(result(i, j) == (uint8_t)((i << 8) + j + 4 * 23 + 1));
            }
        }

        for (int i = 0; i < 4; i++) {
          fprintf(stderr, "Call count for stage %d is %d.\n", i, call_count_staged[i]);
        }

        result = output.realize(128, 128);
        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
              assert(result(i, j) == (uint8_t)((i << 8) + j + 4 * 23 + 1));
            }
        }

        for (int i = 0; i < 4; i++) {
            fprintf(stderr, "Call count for stage %d is %d.\n", i, call_count_staged[i]);
        }
    }

    /*\\{
        // Test out of memory handling.
        Param<float> val;

        Func count_calls;
        count_calls.define_extern("count_calls_with_arg", {cast<uint8_t>(val)}, UInt(8), 2);

        Func f;
        Var x, y, xi, yi;
        f(x, y) = Tuple(count_calls(x, y) + cast<uint8_t>(x), x);
        count_calls.compute_root().memoize();
        f.compute_root().memoize();

        Func g;
        g(x, y) = Tuple(f(x, y)[0] + f(x - 1, y)[0] + f(x + 1, y)[0], f(x, y)[1]);

        Pipeline pipe(g);
        pipe.set_error_handler(record_error);
        pipe.set_custom_allocator(flakey_malloc, simple_free);

        int total_errors = 0;
        int completed = 0;
        for (int trial = 0; trial < 100; trial++) {
            call_count_with_arg = 0;
            error_occured = false;

            val.set(23.0f + trial);
            Realization out = pipe.realize(16, 16);
            if (error_occured) {
                total_errors++;
            } else {
                Buffer<uint8_t> out0 = out[0];
                Buffer<int32_t> out1 = out[1];

                for (int32_t i = 0; i < 16; i++) {
                    for (int32_t j = 0; j < 16; j++) {
                      assert(out0(i, j) == (uint8_t)(3 * (23 + trial) + i + (i - 1) + (i + 1)));
                        assert(out1(i, j) == i);
                    }
                }

                error_occured = false;
                out = pipe.realize(16, 16);
                if (error_occured) {
                    total_errors++;
                } else {
                    out0 = out[0];
                    out1 = out[1];

                    for (int32_t i = 0; i < 16; i++) {
                        for (int32_t j = 0; j < 16; j++) {
                          assert(out0(i, j) == (uint8_t)(3 * (23 + trial) + i + (i - 1) + (i + 1)));
                            assert(out1(i, j) == i);
                        }
                    }
                    assert(call_count_with_arg == 1);
                    completed++;
                }
            }
        }
        fprintf(stderr, "In 100 attempts with flakey malloc, %d errors and %d full completions occured.\n", total_errors, completed);
    }*/

    fprintf(stderr, "Success!\n");
    return 0;
}

#include "Halide.h"
#include "HalideRuntime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

using namespace Halide;

// External functions to track whether the cache is working.

int call_count = 0;

extern "C" HALIDE_EXPORT_SYMBOL int count_calls(halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(42);
    }
    return 0;
}

int call_count_with_arg = 0;

extern "C" HALIDE_EXPORT_SYMBOL int count_calls_with_arg(uint8_t val, halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count_with_arg++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(val);
    }
    return 0;
}

int call_count_with_arg_parallel[8];

extern "C" HALIDE_EXPORT_SYMBOL int count_calls_with_arg_parallel(uint8_t val, halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        call_count_with_arg_parallel[out->dim[2].min]++;
        Halide::Runtime::Buffer<uint8_t>(*out).fill(val);
    }
    return 0;
}

int call_count_staged[4];

extern "C" HALIDE_EXPORT_SYMBOL int count_calls_staged(int32_t stage, uint8_t val, halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        for (int i = 0; i < out->dimensions; i++) {
            in->dim[i] = out->dim[i];
        }
    } else if (!out->is_bounds_query()) {
        assert(stage < static_cast<int32_t>(sizeof(call_count_staged) / sizeof(call_count_staged[0])));
        call_count_staged[stage]++;
        Halide::Runtime::Buffer<uint8_t> out_buf(*out), in_buf(*in);
        out_buf.for_each_value([&](uint8_t &out, uint8_t &in) { out = in + val; }, in_buf);
    }
    return 0;
}

extern "C" HALIDE_EXPORT_SYMBOL int computed_eviction_key(int a) {
    return 2020 + a;
}
HalideExtern_1(int, computed_eviction_key, int);

void simple_free(JITUserContext *user_context, void *ptr) {
    free(ptr);
}

void *flakey_malloc(JITUserContext * /* user_context */, size_t x) {
    if ((rand() % 4) == 0) {
        return nullptr;
    } else {
        return malloc(x);
    }
}

bool error_occured = false;
void record_error(JITUserContext *user_context, const char *msg) {
    error_occured = true;
}

int main(int argc, char **argv) {

    {
        call_count = 0;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        Func f, f_memoized;
        f_memoized() = count_calls(0, 0);
        f() = f_memoized();
        f_memoized.compute_root().memoize();

        Buffer<uint8_t> result1 = f.realize();
        Buffer<uint8_t> result2 = f.realize();

        assert(result1(0) == 42);
        assert(result2(0) == 42);

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
        Buffer<uint8_t> out1 = g.realize({256, 256});
        Buffer<uint8_t> out2 = g.realize({256, 256});

        for (int32_t i = 0; i < 256; i++) {
            for (int32_t j = 0; j < 256; j++) {
                assert(out1(i, j) == 42);
                assert(out2(i, j) == 42);
            }
        }
        assert(call_count == 1);

        coord.set(1);
        Buffer<uint8_t> out3 = g.realize({256, 256});
        Buffer<uint8_t> out4 = g.realize({256, 256});

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

        Buffer<uint8_t> out1 = f.realize({256, 256});
        Buffer<uint8_t> out2 = f.realize({256, 256});

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

        Buffer<uint8_t> out1 = f.realize({256, 256});
        Buffer<uint8_t> out2 = f.realize({256, 256});

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

        Buffer<uint8_t> out1 = f.realize({256, 256});
        Buffer<uint8_t> out2 = f.realize({256, 256});

        val1.set(42);
        Buffer<uint8_t> out3 = f.realize({256, 256});

        val1.set(23);
        Buffer<uint8_t> out4 = f.realize({256, 256});

        val1.set(42);
        Buffer<uint8_t> out5 = f.realize({256, 256});

        val2.set(57);
        Buffer<uint8_t> out6 = f.realize({256, 256});

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
        Buffer<uint8_t> out1 = f.realize({256, 256});
        val.set(23.4f);
        Buffer<uint8_t> out2 = f.realize({256, 256});

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
        Buffer<uint8_t> out1 = f.realize({256, 256});
        val.set(23.4f);
        Buffer<uint8_t> out2 = f.realize({256, 256});

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
        g(x) = f(x);
        h(x) = g(4) + g(index);

        f.compute_root().memoize();
        g.vectorize(x, 8).compute_at(h, x);

        val.set(23.0f);
        index.set(2);
        Buffer<uint8_t> out1 = h.realize({1});

        assert(out1(0) == (uint8_t)(2 * 23 + 4 + 2));
        assert(call_count_with_arg == 3);

        index.set(4);
        out1 = h.realize({1});

        assert(out1(0) == (uint8_t)(2 * 23 + 4 + 4));
        assert(call_count_with_arg == 4);
    }

    {
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
        Realization out = g.realize({128, 128});
        Buffer<uint8_t> out0 = out[0];
        Buffer<int32_t> out1 = out[1];

        for (int32_t i = 0; i < 100; i++) {
            for (int32_t j = 0; j < 100; j++) {
                assert(out0(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
                assert(out1(i, j) == i);
            }
        }
        out = g.realize({128, 128});
        out0 = out[0];
        out1 = out[1];

        for (int32_t i = 0; i < 100; i++) {
            for (int32_t j = 0; j < 100; j++) {
                assert(out0(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
                assert(out1(i, j) == i);
            }
        }
        assert(call_count_with_arg == 1);
    }

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
            Buffer<uint8_t> out1 = g.realize({128, 128});

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }
        // TODO work out an assertion on call count here.
        printf("Call count is %d.\n", call_count_with_arg);

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
            Buffer<uint8_t> out1 = g.realize({128, 128});

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }

        // TODO work out an assertion on call count here.
        printf("Call count before oversize realize is %d.\n", call_count_with_arg);
        call_count_with_arg = 0;

        Buffer<uint8_t> big = g.realize({1024, 1024});
        Buffer<uint8_t> big2 = g.realize({1024, 1024});

        // TODO work out an assertion on call count here.
        printf("Call count after oversize realize is %d.\n", call_count_with_arg);

        call_count_with_arg = 0;
        for (int v = 0; v < 1000; v++) {
            int r = rand() % 256;
            val.set((float)r);
            Buffer<uint8_t> out1 = g.realize({128, 128});

            for (int32_t i = 0; i < 100; i++) {
                for (int32_t j = 0; j < 100; j++) {
                    assert(out1(i, j) == (uint8_t)(3 * r + i + (i - 1) + (i + 1)));
                }
            }
        }

        printf("Call count is %d.\n", call_count_with_arg);

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
        Buffer<uint8_t> out = g.realize({128, 128});

        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
                assert(out(i, j) == (uint8_t)(3 * 23 + i + (i - 1) + (i + 1)));
            }
        }

        // TODO work out an assertion on call counts here.
        for (int i = 0; i < 8; i++) {
            printf("Call count for thread %d is %d.\n", i, call_count_with_arg_parallel[i]);
        }

        // Return cache size to default.
        Internal::JITSharedRuntime::memoization_cache_set_size(0);
    }

    {
        // Test multiple argument memoize_tag. This can be unsafe but
        // models cases where one uses a hash of image data as part of
        // a tag to memoize an expensive computation.
        ImageParam input(UInt(8), 1);
        Param<int> key;
        Func f, g;
        RDom extent(input);

        g() = memoize_tag(sum(input(extent)), key);
        f() = g() + 42;
        g.compute_root().memoize();

        Buffer<uint8_t> in(10);
        input.set(in);

        in.fill(42);

        key.set(0);
        Buffer<uint8_t> result = f.realize();
        assert(result() == (462 % 256));

        // Change image data without channging tag
        in(0) = 41;
        result = f.realize();

        // Result is likely stale. This is not strictly guaranteed due to e.g.
        // cache size. Hence allow correct value to make test express the
        // contract.
        assert((result() == (462 % 256)) ||
               (result() == (461 % 256)));

        // Change tag, thus ensuring correct result.
        key.set(1);
        result = f.realize();
        assert(result() == (461 % 256));
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
        output(_) = stage[3](_);
        val.set(23.0f);
        Buffer<uint8_t> result = output.realize({128, 128});

        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
                assert(result(i, j) == (uint8_t)((i << 8) + j + 4 * 23));
            }
        }

        for (int i = 0; i < 4; i++) {
            printf("Call count for stage %d is %d.\n", i, call_count_staged[i]);
        }

        result = output.realize({128, 128});
        for (int32_t i = 0; i < 128; i++) {
            for (int32_t j = 0; j < 128; j++) {
                assert(result(i, j) == (uint8_t)((i << 8) + j + 4 * 23));
            }
        }

        for (int i = 0; i < 4; i++) {
            printf("Call count for stage %d is %d.\n", i, call_count_staged[i]);
        }
    }

    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support custom allocators.\n");
        return 0;
    } else {
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
        pipe.jit_handlers().custom_error = record_error;
        pipe.jit_handlers().custom_malloc = flakey_malloc;
        pipe.jit_handlers().custom_free = simple_free;

        int total_errors = 0;
        int completed = 0;
        for (int trial = 0; trial < 100; trial++) {
            call_count_with_arg = 0;
            error_occured = false;

            val.set(23.0f + trial);
            Realization out = pipe.realize({16, 16});
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
                out = pipe.realize({16, 16});
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

        printf("In 100 attempts with flakey malloc, %d errors and %d full completions occured.\n", total_errors, completed);
    }

    {
        call_count = 0;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        ImageParam input(UInt(8), 1);
        Func f, f_memoized;
        f_memoized() = count_calls(0, 0) + cast<uint8_t>(input.dim(0).extent());
        f_memoized.compute_root().memoize();
        f() = f_memoized();

        Buffer<uint8_t> in_one(1);
        input.set(in_one);

        Buffer<uint8_t> result1 = f.realize();
        Buffer<uint8_t> result2 = f.realize();

        assert(result1(0) == 43);
        assert(result2(0) == 43);

        assert(call_count == 1);

        Buffer<uint8_t> in_ten(10);
        input.set(in_ten);

        result1 = f.realize();
        result2 = f.realize();

        assert(result1(0) == 52);
        assert(result2(0) == 52);

        assert(call_count == 2);
    }

    // Test cache eviction.
    {
        call_count = 0;
        Func count_calls;
        count_calls.define_extern("count_calls", {}, UInt(8), 2);

        Param<void *> p;
        Func f, memoized_one, memoized_two, memoized_three;
        memoized_one() = count_calls(0, 0);
        memoized_two() = count_calls(1, 1);
        memoized_three() = count_calls(3, 3);
        memoized_one.compute_root().memoize(EvictionKey(1));
        memoized_two.compute_root().memoize(EvictionKey(p));
        // The called extern here would usually take user_context and extact a value
        // from within, but JIT mostly subsumes user_context, so this is just an example.
        memoized_three.compute_root().memoize(EvictionKey(computed_eviction_key(5)));
        f() = memoized_one() + memoized_two() + memoized_three();

        p.set((void *)&call_count);
        Buffer<uint8_t> result1 = f.realize();
        Buffer<uint8_t> result2 = f.realize();

        assert(result1(0) == 126);
        assert(result2(0) == 126);

        assert(call_count == 3);

        Internal::JITSharedRuntime::memoization_cache_evict(1);
        result1 = f.realize();
        assert(result1(0) == 126);

        assert(call_count == 4);

        Internal::JITSharedRuntime::memoization_cache_evict(1);
        result1 = f.realize();
        assert(result1(0) == 126);

        assert(call_count == 5);

        Internal::JITSharedRuntime::memoization_cache_evict(1);
        Internal::JITSharedRuntime::memoization_cache_evict((uint64_t)(uintptr_t)&call_count);
        result1 = f.realize();
        assert(result1(0) == 126);

        assert(call_count == 7);

        Internal::JITSharedRuntime::memoization_cache_evict(2025);
        result1 = f.realize();
        assert(result1(0) == 126);

        assert(call_count == 8);
    }

    printf("Success!\n");
    return 0;
}

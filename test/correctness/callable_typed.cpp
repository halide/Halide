#include "Halide.h"
#include <iostream>
#include <stdio.h>

using namespace Halide;

namespace {

void check(int r) {
    assert(r == 0);
}

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
    return nullptr;
}

int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL float my_extern_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, my_extern_func, int, float);

}  // namespace

int main(int argc, char **argv) {
    const Target t = get_jit_target_from_environment();

    {
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

        // Note that we can't reliably infer the std::function<> signature in all
        // cases, since some of the arguments may not be statically typed (e.g. Param<void>),
        // but `make_std_function` will fail at runtime if the template arguments
        // don't match what is required.
        auto c = f.compile_to_callable({p_img, p_int, p_float}, t)
                     .make_std_function<Buffer<uint8_t>, int, float, Buffer<uint8_t>>();

        {
            Buffer<uint8_t> out1(10, 10);
            check(c(in1, 42, 1.0f, out1));

            Buffer<uint8_t> out2(10, 10);
            check(c(in2, 22, 2.0f, out2));

            Buffer<uint8_t> out3(10, 10);
            check(c(in1, 12, 1.0f, out3));

            Buffer<uint8_t> out4(10, 10);
            check(c(in2, 16, 1.0f, out4));

            for (int i = 0; i < 10; i++) {
                for (int j = 0; j < 10; j++) {
                    assert(out1(i, j) == i + j * 10 + 42);
                    assert(out2(i, j) == i * 10 + j + 11);
                    assert(out3(i, j) == i + j * 10 + 12);
                    assert(out4(i, j) == i * 10 + j + 16);
                }
            }
        }

        {
            // Test bounds inference
            Buffer<uint8_t> in_bounds(nullptr, 1, 1);
            Buffer<uint8_t> out_bounds(nullptr, 20, 20);

            check(c(in_bounds, 42, 1.0f, out_bounds));

            assert(in_bounds.defined());
            assert(in_bounds.dim(0).extent() == 20);
            assert(in_bounds.dim(1).extent() == 20);
            assert(in1.dim(0).extent() == 10);
            assert(in1.dim(1).extent() == 10);
        }
    }

    // Override Halide's malloc and free (except under wasm),
    // make sure that Callable freezes the values
    if (t.arch != Target::WebAssembly) {
        custom_malloc_called = false;
        custom_free_called = false;

        Func f, g;
        Var x;

        f(x) = x;
        g(x) = f(x);
        f.compute_root();

        g.jit_handlers().custom_malloc = my_malloc;
        g.jit_handlers().custom_free = my_free;

        auto c = g.compile_to_callable({})
                     .make_std_function<Buffer<int>>();

        // Changing g's handlers shouldn't affect any existing Callables
        g.jit_handlers().custom_malloc = mischievous_malloc;

        Buffer<int> im(100000);
        check(c(im));

        assert(custom_malloc_called);
        assert(custom_free_called);
    }

    // Check that Param<void*> works with Callables
    if (t.arch != Target::WebAssembly) {
        Func f("f"), g("g");
        Var x("x");
        Param<void *> handle("handle");

        f(x) = reinterpret<uint64_t>(handle);

        g(x) = reinterpret<uint64_t>(handle);
        g.vectorize(x, 4);

        // Create/use a dummy JITUserContext here just to test that
        // passing one explicitly works correctly.
        auto cf = f.compile_to_callable({handle})
                      .make_std_function<JITUserContext *, int *, Buffer<uint64_t>>();
        auto cg = g.compile_to_callable({handle})
                      .make_std_function<int *, Buffer<uint64_t>>();

        int foo = 0;

        Buffer<uint64_t> out1(4);
        JITUserContext empty;
        check(cf(&empty, &foo, out1));

        Buffer<uint64_t> out2(4);
        check(cg(&foo, out2));

        uint64_t correct = (uint64_t)((uintptr_t)(&foo));

        for (int x = 0; x < out1.width(); x++) {
            if (out1(x) != correct) {
                printf("out1(%d) = %llu instead of %llu\n",
                       x,
                       (long long unsigned)out1(x),
                       (long long unsigned)correct);
                exit(1);
            }
            if (out2(x) != correct) {
                printf("out2(%d) = %llu instead of %llu\n",
                       x,
                       (long long unsigned)out2(x),
                       (long long unsigned)correct);
                exit(1);
            }
        }
    }

    // Check that JITExterns works with Callables
    if (t.arch != Target::WebAssembly) {
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

        auto c = p.compile_to_callable({})
                     .make_std_function<Buffer<float>>();

        // Changing g's jit_externs shouldn't affect any existing Callables
        p.set_jit_externs({});

        Buffer<float> imf(32, 32);
        check(c(imf));

        // Check the result was what we expected
        for (int i = 0; i < 32; i++) {
            for (int j = 0; j < 32; j++) {
                float correct = (float)(i * j);
                float delta = imf(i, j) - correct;
                if (delta < -0.001 || delta > 0.001) {
                    printf("imf[%d, %d] = %f instead of %f\n", i, j, imf(i, j), correct);
                    exit(1);
                }
            }
        }

        if (call_counter != 32 * 32) {
            printf("In pipeline_set_jit_externs_func, my_func was called %d times instead of %d\n", call_counter, 32 * 32);
            exit(1);
        }
    }

    printf("Success!\n");
}

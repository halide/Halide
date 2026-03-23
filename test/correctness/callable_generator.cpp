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

int call_counter = 0;
extern "C" HALIDE_EXPORT_SYMBOL float my_extern_func(int x, float y) {
    call_counter++;
    return x * y;
}
HalideExtern_2(float, my_extern_func, int, float);

}  // namespace

int main(int argc, char **argv) {
    const Target t = get_jit_target_from_environment();
    const GeneratorContext context(t);

    {
        class TestGen1 : public Generator<TestGen1> {
        public:
            Input<Buffer<uint8_t, 2>> img_{"img"};
            Input<int32_t> int_{"int"};
            Input<float> float_{"float"};

            Output<Buffer<uint8_t, 2>> out_{"out"};

            void generate() {
                Var x("x"), y("y");
                out_(x, y) = img_(x, y) + cast<uint8_t>(int_ / float_);
            }
        };

        Buffer<uint8_t> in1(10, 10);
        Buffer<uint8_t> in2(10, 10);

        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 10; j++) {
                in1(i, j) = i + j * 10;
                in2(i, j) = i * 10 + j;
            }
        }

        auto gen = TestGen1::create(context);
        Callable c = gen->compile_to_callable();

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

    // Override Halide's malloc and free (except under wasm),
    // make sure that Callable freezes the values
    if (t.arch != Target::WebAssembly) {
        custom_malloc_called = false;
        custom_free_called = false;

        class TestGen2 : public Generator<TestGen2> {
        public:
            Output<Buffer<int, 1>> out_{"out"};

            void generate() {
                Var x("x");

                Func f;
                f(x) = x;

                out_(x) = f(x);

                f.compute_root();
            }
        };

        JITHandlers my_jit_handlers;
        my_jit_handlers.custom_malloc = my_malloc;
        my_jit_handlers.custom_free = my_free;

        auto gen = TestGen2::create(context);
        Callable c = gen->compile_to_callable(&my_jit_handlers);

        Buffer<int> im(100000);
        check(c(im));

        assert(custom_malloc_called);
        assert(custom_free_called);
    }

    // Check that Param<void*> works with Callables
    if (t.arch != Target::WebAssembly) {
        class TestGen3 : public Generator<TestGen3> {
        public:
            GeneratorParam<bool> vectorize_{"vectorize", false};

            Input<void *> handle_{"handle"};
            Output<Buffer<uint64_t, 1>> out_{"out"};

            void generate() {
                Var x("x");

                out_(x) = reinterpret<uint64_t>(handle_);
                if (vectorize_) {
                    out_.vectorize(x, 4);
                }
            }
        };

        auto gen_1 = TestGen3::create(context);
        gen_1->vectorize_.set(false);

        auto gen_2 = TestGen3::create(context);
        gen_2->vectorize_.set(true);

        Callable c1 = gen_1->compile_to_callable();
        Callable c2 = gen_2->compile_to_callable();

        int foo = 0;

        Buffer<uint64_t> out1(4);
        // Create a dummy JITUserContext here just to test that
        // passing one explicitly works correctly.
        JITUserContext empty;
        check(c1(&empty, &foo, out1));

        Buffer<uint64_t> out2(4);
        check(c2(&foo, out2));

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

        class TestGen4 : public Generator<TestGen4> {
        public:
            Output<Buffer<float, 2>> out_{"out"};

            void generate() {
                Func f;
                f.define_extern("extern_func", {user_context_value()}, Float(32), 2);

                Var x("x"), y("y");
                out_(x, y) = f(x, y);
            }
        };

        Var x, y;
        Func monitor;
        monitor(x, y) = my_extern_func(x, cast<float>(y));
        const std::map<std::string, JITExtern> my_jit_externs = {
            {"extern_func", JITExtern{monitor}}};

        auto gen = TestGen4::create(context);
        Callable c = gen->compile_to_callable(nullptr, &my_jit_externs);

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

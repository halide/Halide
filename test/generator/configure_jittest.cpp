#include "Halide.h"

// Include the machine-generated .stub.h header file.
#include "configure.stub.h"

using namespace Halide;

const int kSize = 32;

void verify(const Buffer<int32_t, 3> &img, float compiletime_factor, float runtime_factor, int channels) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {
    GeneratorContext context(get_jit_target_from_environment());

    Buffer<int, 3> input(kSize, kSize, 3);
    input.for_each_element([&](int x, int y, int c) {
        input(x, y, c) = (x * 3 + y * 5 + c * 7);
    });

    std::vector<Buffer<uint8_t, 2>> extras;
    int extra_value = 0;
    for (int i = 0; i < 3; ++i) {
        extras.push_back(Buffer<uint8_t, 2>(kSize, kSize));
        extras.back().fill((uint8_t)i);
        extra_value += i;
    }

    constexpr uint16_t typed_extra_value = 4;
    Buffer<int16_t, 2> typed_extra(kSize, kSize);
    typed_extra.fill(typed_extra_value);

    constexpr int extra_scalar = 7;
    constexpr int8_t extra_dynamic_scalar = 13;
    constexpr uint16_t extra_func_value = 5;

    constexpr int bias = 1;

    extra_value += extra_scalar + extra_dynamic_scalar + extra_func_value + typed_extra_value + bias;

    // Use a Generator Stub to create the Halide IR,
    // then call realize() to JIT and execute it.
    {
        // When calling a Stub, Func inputs must be actual Halide::Func.
        Var x, y, c;
        Func func_extra;
        func_extra(x, y, c) = cast<uint16_t>(extra_func_value);

        auto result = configure::generate(context, configure::Inputs{
                                                       input,
                                                       bias,
                                                       extras[0], extras[1], extras[2],
                                                       typed_extra,
                                                       func_extra,
                                                       extra_scalar,
                                                       cast<int8_t>(extra_dynamic_scalar)});

        Buffer<int32_t, 3> output = result.output.realize({kSize, kSize, 3});
        Buffer<float, 3> extra_buffer_output = result.extra_buffer_output.realize({kSize, kSize, 3});
        Buffer<double, 2> extra_func_output = result.extra_func_output.realize({kSize, kSize});

        output.for_each_element([&](int x, int y, int c) {
            assert(output(x, y, c) == input(x, y, c) + extra_value);
        });

        extra_buffer_output.for_each_element([&](int x, int y, int c) {
            assert(extra_buffer_output(x, y, c) == output(x, y, c));
        });

        extra_func_output.for_each_element([&](int x, int y) {
            assert(extra_func_output(x, y) == output(x, y, 0));
        });
    }

    // Alternately, instead of using Generator Stubs, we can just use the Callable interface.
    // We can call this on any Generator that is registered in the current process.
    {
        Callable configure = create_callable_from_generator(context, "configure");

        Buffer<int, 3> output(kSize, kSize, 3);
        Buffer<float, 3> extra_buffer_output(kSize, kSize, 3);
        Buffer<double, 2> extra_func_output(kSize, kSize);

        // All inputs to a Callable must be fully realized, so any Func inputs
        // that the Generator has implicitly become Buffer inputs of the same type
        // and dimensionality.
        Buffer<uint16_t, 3> func_extra(kSize, kSize, 3);
        func_extra.fill(extra_func_value);

        int r = configure(input, bias,
                          // extra inputs are in the order they were added, after all predeclared inputs
                          extras[0], extras[1], extras[2],
                          typed_extra,
                          func_extra,
                          extra_scalar,
                          extra_dynamic_scalar,
                          output,
                          // extra outputs are in the order they were added, after all predeclared outputs
                          extra_buffer_output,
                          extra_func_output);
        assert(r == 0);

        output.for_each_element([&](int x, int y, int c) {
            assert(output(x, y, c) == input(x, y, c) + extra_value);
        });

        extra_buffer_output.for_each_element([&](int x, int y, int c) {
            assert(extra_buffer_output(x, y, c) == output(x, y, c));
        });

        extra_func_output.for_each_element([&](int x, int y) {
            assert(extra_func_output(x, y) == output(x, y, 0));
        });
    }

    // We can also make an explicitly-typed std::function if we prefer.
    {
        auto configure = create_callable_from_generator(context, "configure")
                             .make_std_function<
                                 Buffer<int, 3>,
                                 int32_t,
                                 Buffer<uint8_t, 2>,
                                 Buffer<uint8_t, 2>,
                                 Buffer<uint8_t, 2>,
                                 Buffer<int16_t, 2>,
                                 Buffer<uint16_t, 3>,
                                 int32_t,
                                 int8_t,
                                 Buffer<int, 3>,
                                 Buffer<float, 3>,
                                 Buffer<double, 2>>();

        Buffer<int, 3> output(kSize, kSize, 3);
        Buffer<float, 3> extra_buffer_output(kSize, kSize, 3);
        Buffer<double, 2> extra_func_output(kSize, kSize);

        // All inputs to a Callable must be fully realized, so any Func inputs
        // that the Generator has implicitly become Buffer inputs of the same type
        // and dimensionality.
        Buffer<uint16_t, 3> func_extra(kSize, kSize, 3);
        func_extra.fill(extra_func_value);

        int r = configure(input, bias,
                          // extra inputs are in the order they were added, after all predeclared inputs
                          extras[0], extras[1], extras[2],
                          typed_extra,
                          func_extra,
                          extra_scalar,
                          extra_dynamic_scalar,
                          output,
                          // extra outputs are in the order they were added, after all predeclared outputs
                          extra_buffer_output,
                          extra_func_output);
        assert(r == 0);

        output.for_each_element([&](int x, int y, int c) {
            assert(output(x, y, c) == input(x, y, c) + extra_value);
        });

        extra_buffer_output.for_each_element([&](int x, int y, int c) {
            assert(extra_buffer_output(x, y, c) == output(x, y, c));
        });

        extra_func_output.for_each_element([&](int x, int y) {
            assert(extra_func_output(x, y) == output(x, y, 0));
        });
    }

    printf("Success!\n");
    return 0;
}

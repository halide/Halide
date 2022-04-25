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

    Buffer<int16_t, 2> typed_extra(kSize, kSize);
    typed_extra.fill(4);
    extra_value += 4;

    Var x, y, c;
    Func func_extra;
    func_extra(x, y, c) = cast<uint16_t>(5);
    extra_value += 5;

    const int extra_scalar = 7;
    const int8_t extra_dynamic_scalar = 13;
    extra_value += extra_scalar + extra_dynamic_scalar;

    const int bias = 1;
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
        assert(output(x, y, c) == input(x, y, c) + bias + extra_value);
    });

    extra_buffer_output.for_each_element([&](int x, int y, int c) {
        assert(extra_buffer_output(x, y, c) == output(x, y, c));
    });

    extra_func_output.for_each_element([&](int x, int y) {
        assert(extra_func_output(x, y) == output(x, y, 0));
    });

    printf("Success!\n");
    return 0;
}

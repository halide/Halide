#include <algorithm>
#include <cstdio>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "stubtest.h"

using Halide::Runtime::Buffer;

const int kSize = 32;

template<typename Type>
Buffer<Type, 3> make_image(int extra) {
    Buffer<Type, 3> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c + extra);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType, int Dims>
void verify(const Buffer<InputType, Dims> &input, float float_arg, int int_arg, const Buffer<OutputType, Dims> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch: %dx%d vs %dx%d\n", input.width(), input.height(), output.width(), output.height());
        exit(1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f (input = %f)\n", x, y, c, (double)actual, (double)expected, (double)input(x, y, c));
                    exit(1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    Buffer<uint8_t, 3> buffer_input = make_image<uint8_t>(0);
    Buffer<float, 3> simple_input = make_image<float>(0);
    Buffer<float, 3> array_input0 = make_image<float>(0);
    Buffer<float, 3> array_input1 = make_image<float>(1);
    Buffer<float, 3> typed_buffer_output(kSize, kSize, 3);
    Buffer<float, 3> untyped_buffer_output(kSize, kSize, 3);
    Buffer<float, 3> tupled_output0(kSize, kSize, 3);
    Buffer<int32_t, 3> tupled_output1(kSize, kSize, 3);
    Buffer<uint8_t, 3> array_buffer_input0 = make_image<uint8_t>(0);
    Buffer<uint8_t, 3> array_buffer_input1 = make_image<uint8_t>(1);
    Buffer<float, 3> simple_output(kSize, kSize, 3);
    // TODO: see Issues #3709, #3967
    Buffer<void, 3> float16_output(halide_type_t(halide_type_float, 16), kSize, kSize, 3);
    Buffer<void, 3> bfloat16_output(halide_type_t(halide_type_bfloat, 16), kSize, kSize, 3);
    Buffer<float, 3> tuple_output0(kSize, kSize, 3), tuple_output1(kSize, kSize, 3);
    Buffer<int16_t, 3> array_output0(kSize, kSize, 3), array_output1(kSize, kSize, 3);
    Buffer<uint8_t, 3> static_compiled_buffer_output(kSize, kSize, 3);
    Buffer<uint8_t, 3> array_buffer_output0(kSize, kSize, 3), array_buffer_output1(kSize, kSize, 3);

    stubtest(
        buffer_input,
        buffer_input,
        array_buffer_input0, array_buffer_input1,
        simple_input,
        array_input0, array_input1,
        1.25f,
        33,
        66,
        simple_output,
        tuple_output0, tuple_output1,
        array_output0, array_output1,
        typed_buffer_output,
        untyped_buffer_output,
        tupled_output0, tupled_output1,
        static_compiled_buffer_output,
        array_buffer_output0, array_buffer_output1,
        float16_output,
        bfloat16_output);

    verify(buffer_input, 1.f, 0, typed_buffer_output);
    verify(buffer_input, 1.f, 0, untyped_buffer_output);
    verify(simple_input, 1.f, 0, simple_output);
    verify(simple_input, 1.f, 0, tupled_output0);
    verify(simple_input, 1.f, 1, tupled_output1);
    verify(array_input0, 1.f, 0, simple_output);
    verify(array_input0, 1.25f, 0, tuple_output0);
    verify(array_input0, 1.25f, 33, tuple_output1);
    verify(array_input0, 1.0f, 33, array_output0);
    verify(array_input1, 1.0f, 66, array_output1);
    verify(buffer_input, 1.0f, 42, static_compiled_buffer_output);
    verify(array_buffer_input0, 1.f, 1, array_buffer_output0);
    verify(array_buffer_input1, 1.f, 2, array_buffer_output1);

    printf("Success!\n");
    return 0;
}

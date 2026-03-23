#include <cstdio>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "halide_benchmark.h"

#include "stubuser.h"
#include "stubuser_auto.h"

using namespace Halide::Runtime;

const int kSize = 32;

template<typename Type>
Buffer<Type, 3> make_image() {
    Buffer<Type, 3> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

const float kFloatArg = 1.234f;
const int kIntArg = 33;
const float kOffset = 2.f;

template<typename InputType, typename OutputType>
void verify(const Buffer<InputType, 3> &input, float float_arg, int int_arg, float offset, const Buffer<OutputType, 3> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch\n");
        exit(1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg + offset);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f\n", x, y, c, (double)actual, (double)expected);
                    exit(1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {

    Buffer<uint8_t, 3> input = make_image<uint8_t>();
    Buffer<uint8_t, 3> calculated_output(kSize, kSize, 3);
    Buffer<float, 3> float32_buffer_output(kSize, kSize, 3);
    Buffer<void, 3> int32_buffer_output(halide_type_t(halide_type_int, 32), kSize, kSize, 3);
    Buffer<uint8_t, 3> array_test_output(kSize, kSize, 3);
    Buffer<float, 3> tupled_output0(kSize, kSize, 3);
    Buffer<int32_t, 3> tupled_output1(kSize, kSize, 3);
    Buffer<int, 3> int_output(kSize, kSize, 3);
    // TODO: see Issues #3709, #3967
    Buffer<void, 3> float16_output(halide_type_t(halide_type_float, 16), kSize, kSize, 3);
    Buffer<void, 3> bfloat16_output(halide_type_t(halide_type_bfloat, 16), kSize, kSize, 3);

    struct FnInfo {
        decltype(&stubuser) f;
        const char *const name;
    };
    FnInfo fns[2] = {{stubuser, "stubuser"}, {stubuser_auto, "stubuser_auto"}};
    for (auto f : fns) {
        printf("Testing %s...\n", f.name);
        f.f(input, calculated_output, float32_buffer_output, int32_buffer_output,
            array_test_output, tupled_output0, tupled_output1, int_output,
            float16_output, bfloat16_output);
        verify(input, kFloatArg, kIntArg, kOffset, calculated_output);
        verify(input, 1.f, 0, 0.f, float32_buffer_output);
        verify<uint8_t, int32_t>(input, 1.f, 0, 0.f, int32_buffer_output);
        verify(input, 1.f, 0, 2, array_test_output);
        verify(input, 1.f, 0, 0, tupled_output0);
        verify(input, 1.f, 1, 3, int_output);
    }

    printf("Success!\n");
    return 0;
}

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include "stubuser.h"

using namespace Halide::Runtime;

const int kSize = 32;

template<typename Type>
Buffer<Type> make_image() {
    Buffer<Type> im(kSize, kSize, 3);
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
void verify(const Buffer<InputType> &input, float float_arg, int int_arg, float offset, const Buffer<OutputType> &output) {
    if (input.width() != output.width() ||
        input.height() != output.height()) {
        fprintf(stderr, "size mismatch\n");
        exit(-1);
    }
    int channels = std::max(1, std::min(input.channels(), output.channels()));
    for (int x = 0; x < output.width(); x++) {
        for (int y = 0; y < output.height(); y++) {
            for (int c = 0; c < channels; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg + offset);
                const OutputType actual = output(x, y, c);
                if (expected != actual) {
                    fprintf(stderr, "img[%d, %d, %d] = %f, expected %f\n", x, y, c, (double)actual, (double)expected);
                    exit(-1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {

  Buffer<uint8_t> input = make_image<uint8_t>();
  Buffer<uint8_t> calculated_output(kSize, kSize, 3);
  Buffer<float> float32_buffer_output(kSize, kSize, 3);
  Buffer<> int32_buffer_output(halide_type_t(halide_type_int, 32), kSize, kSize, 3);
  Buffer<uint8_t> array_test_output(kSize, kSize, 3);
  Buffer<float> tupled_output0(kSize, kSize, 3);
  Buffer<int32_t> tupled_output1(kSize, kSize, 3);
  Buffer<int> int_output(kSize, kSize, 3);

  stubuser(input, calculated_output, float32_buffer_output, int32_buffer_output,
    array_test_output, tupled_output0, tupled_output1, int_output);
  verify(input, kFloatArg, kIntArg, kOffset, calculated_output);
  verify(input, 1.f, 0, 0.f, float32_buffer_output);
  verify<uint8_t, int32_t>(input, 1.f, 0, 0.f, int32_buffer_output);
  verify(input, 1.f, 0, 2, array_test_output);
  verify(input, 1.f, 0, 0, tupled_output0);
  verify(input, 1.f, 1, 3, int_output);

  printf("Success!\n");
  return 0;
}

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include "stubuser.h"

using namespace Halide;

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
void verify(const Buffer<InputType> &input, const Buffer<OutputType> &output) {
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * kFloatArg + kIntArg) + kOffset;
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
  Buffer<uint8_t> output(kSize, kSize, 3);

  stubuser(input, output);
  verify(input, output);

  printf("Success!\n");
  return 0;
}

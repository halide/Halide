#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "metadata_tester.h"
#include "static_image.h"

const int kSize = 32;

template<typename Type>
Image<Type> MakeImage() {
    Image<Type> im(kSize, kSize, 3);
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                im(x, y, c) = static_cast<Type>(x + y + c);
            }
        }
    }
    return im;
}

template<typename InputType, typename OutputType>
void verify(const Image<InputType> &input, float float_arg, int int_arg, const Image<OutputType> &output) {
    for (int x = 0; x < kSize; x++) {
        for (int y = 0; y < kSize; y++) {
            for (int c = 0; c < 3; c++) {
                const OutputType expected = static_cast<OutputType>(input(x, y, c) * float_arg + int_arg);
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

  const Image<uint8_t> input = MakeImage<uint8_t>();
  const float float_arg = 1.234f;
  const int int_arg = 765;

  Image<float> output(kSize, kSize, 3);

  int result;

  result = metadata_tester(input, float_arg, int_arg, output);
  if (result != 0) {
    fprintf(stderr, "Test failure %d\n", result);
    exit(-1);
  }

  verify(input, float_arg, int_arg, output);

  const halide_filter_metadata_t &md = metadata_tester_metadata;

  // target will vary depending on where we are testing, but probably
  // will contain "x86" or "arm".
  if (!strstr(md.target, "x86") && !strstr(md.target, "arm")) {
    fprintf(stderr, "Expected x86 or arm, Actual %s\n", md.target);
    exit(-1);
  }

  if (md.num_inputs != 3) {
    fprintf(stderr, "Expected %d, Actual %d\n", 3, (int) md.num_inputs);
    exit(-1);
  }

  if (md.num_outputs != 1) {
    fprintf(stderr, "Expected %d, Actual %d\n", 1, (int) md.num_outputs);
    exit(-1);
  }

  printf("Success!\n");
  return 0;
}
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "example.h"
#include "static_image.h"

// Standard header file doesn't declare the _jit_wrapper entry point.
extern "C" int example_jit_wrapper(void** args);

const int kSize = 32;

void verify(const Image<int> &img, float compiletime_factor, float runtime_factor, int channels) {
    for (int i = 0; i < kSize; i++) {
        for (int j = 0; j < kSize; j++) {
            for (int c = 0; c < channels; c++) {
                int expected = (int32_t)(compiletime_factor * runtime_factor * c * (i > j ? i : j));
                if (img(i, j, c) != expected) {
                    printf("img[%d, %d, %d] = %d (expected %d)\n", i, j, c, img(i, j, c), expected);
                    exit(-1);
                }
            }
        }
    }
}

int main(int argc, char **argv) {

  int result;
  Image<int32_t> output(kSize, kSize, 3);

  // For Ahead-of-time compilation, we don't get to customize any GeneratorParams:
  // they were baked into the object code by our build system. These are the default values
  // for Example (replicated here to use in verify()).
  const float compiletime_factor = 1.0f;
  const int channels = 3;

  // We can, of course, pass whatever values for Param/ImageParam that we like.
  result = example(3.3f, output);
  if (result != 0) {
    fprintf(stderr, "Result: %d\n", result);
    exit(-1);
  }
  verify(output, compiletime_factor, 3.3f, channels);

  result = example(-1.234f, output);
  if (result != 0) {
    fprintf(stderr, "Result: %d\n", result);
    exit(-1);
  }
  verify(output, compiletime_factor, -1.234f, channels);

  // verify that calling via the _jit_wrapper entry point
  // also produces the correct result
  float arg0 = 1.234f;
  buffer_t arg1 = *output;
  void* args[2] = { &arg0, &arg1 };
  result = example_jit_wrapper(args);
  if (result != 0) {
    fprintf(stderr, "Result: %d\n", result);
    exit(-1);
  }
  verify(output, compiletime_factor, 1.234f, channels);

  printf("Success!\n");
  return 0;
}

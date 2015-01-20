#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "example.h"
#include "static_image.h"

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

  Image<int32_t> output(kSize, kSize, 3);

  // For Ahead-of-time compilation, we don't get to customize any GeneratorParams:
  // they were baked into the object code by our build system. These are the default values
  // for Example (replicated here to use in verify()).
  const float compiletime_factor = 1.0f;
  const int channels = 3;

  // We can, of course, pass whatever values for Param/ImageParam that we like.
  example(3.3f, output);
  verify(output, compiletime_factor, 3.3f, channels);

  example(-1.234f, output);
  verify(output, compiletime_factor, -1.234f, channels);

  printf("Success!\n");
  return 0;
}

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <math.h>
#include <stdio.h>

#include "buildmethod.h"

using namespace Halide::Runtime;

const int kSize = 32;

void verify(const Buffer<int32_t> &img, float compiletime_factor, float runtime_factor) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {
  Buffer<int32_t> output(kSize, kSize, 3);

  const float compiletime_factor = 1.0f;

  buildmethod(3.3245f, output);
  verify(output, compiletime_factor, 3.3245f);

  printf("Success!\n");
  return 0;
}

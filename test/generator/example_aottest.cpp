#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "example.h"

using namespace Halide::Runtime;

const int kSize = 32;

void verify(const Buffer<int32_t, 3> &img, float compiletime_factor, float runtime_factor, int channels) {
    img.for_each_element([=](int x, int y, int c) {
        int expected = (int32_t)(compiletime_factor * runtime_factor * c * (x > y ? x : y));
        int actual = img(x, y, c);
        assert(expected == actual);
    });
}

int main(int argc, char **argv) {

    Buffer<int32_t, 3> output(kSize, kSize, 3);

    // For Ahead-of-time compilation, we don't get to customize any GeneratorParams:
    // they were baked into the object code by our build system. These are the default values
    // for Example (replicated here to use in verify()).
    const float compiletime_factor = 1.0f;
    const int channels = 3;

    // We can, of course, pass whatever values for Param/ImageParam that we like.
    example(3.3245f, output);
    verify(output, compiletime_factor, 3.3245f, channels);

    example(-1.234f, output);
    verify(output, compiletime_factor, -1.234f, channels);

    printf("Success!\n");
    return 0;
}

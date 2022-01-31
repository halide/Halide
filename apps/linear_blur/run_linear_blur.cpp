#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "linear_blur.h"
#include "simple_blur.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: ./linear_blur use_linear input.png output.png\n");
        return 0;
    }

    int use_linear = atoi(argv[1]);

    Buffer<float, 3> input = load_and_convert_image(argv[2]);
    Buffer<float, 3> output = Buffer<float>::make_with_shape_of(input);

    // Call either the simple or linear-corrected blur at runtime,
    // mainly to demonstrate how simple_blur can be used either standalone
    // or fused into another Generator.
    if (use_linear) {
        printf("Using linear blur...\n");
        linear_blur(input, output);
    } else {
        printf("Using simple blur...\n");
        simple_blur(input, input.width(), input.height(), output);
    }

    convert_and_save_image(output, argv[3]);

    return 0;
}

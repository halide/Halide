#include <cstdio>
#include <cstdlib>

#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "halide_image_io.h"

#include "add.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("Usage: %s in offset out\n", argv[0]);
        return 1;
    }

    // In baremetal target, some image file formats such as ppm/pgm are supported but others are not.
    // e.g. PNG and JPEG are unavailable unless you integrate your own libpng and libjpeg.
    Halide::Runtime::Buffer<uint8_t, 2> input = load_and_convert_image(argv[1]);

    Halide::Runtime::Buffer<uint8_t, 2> output(input.width(), input.height());

    uint8_t offset = std::atoi(argv[2]);
    add(input, offset, output);

    convert_and_save_image(output, argv[3]);

    printf("Success!\n");
    return 0;
}

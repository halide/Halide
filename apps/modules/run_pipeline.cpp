#include "pipeline.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide;

int main(int argc, char **argv) {

    if (argc != 3) {
        printf("Usage: ./run_pipeline input.png output.png\n");
        return 0;
    }

    Image<uint8_t> input = Tools::load_image(argv[1]);
    Image<uint8_t> output(input.width(), input.height(), 1);

    if (pipeline(input, output) != 0) {
        printf("Failure.\n");
        return 1;
    }

    Tools::save_image(output, argv[2]);

    printf("Success!\n");

    return 0;
}

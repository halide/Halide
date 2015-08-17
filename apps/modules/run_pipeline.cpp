#include "pipeline.h"

#include "halide_image.h"
#include "halide_image_io.h"

using namespace Halide::Tools;

int main(int argc, char **argv) {

    if (argc != 3) {
        printf("Usage: ./run_pipeline input.png output.png\n");
        return 0;
    }

    Image<uint8_t> input = load_image(argv[1]);
    Image<uint8_t> output(input.width(), input.height(), 1);

    pipeline(input, output);

    save_image(output, argv[2]);

    printf("Success!\n");

    return 0;
}

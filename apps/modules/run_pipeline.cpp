#include "pipeline.h"
#include "static_image.h"
#include "image_io.h"

int main(int argc, char **argv) {

    if (argc != 3) {
        printf("Usage: ./run_pipeline input.png output.png\n");
        return 0;
    }

    Image<uint8_t> input = load<uint8_t>(argv[1]);
    Image<uint8_t> output(input.width(), input.height(), 1);

    pipeline(input, output);

    save(output, argv[2]);

    printf("Success!\n");

    return 0;
}

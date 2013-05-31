extern "C" {
    //#include "halide_blur.h"
#include "halide_blur_cvl.h"
}
#include <stdio.h>
#include "static_image.h"
#include "image_io.h"

int main(int argc, char **argv) {

    if (argc < 3) {
        printf("Usage: ./process input.png output.png\n"
               "e.g.: ./process input.png output.png\n");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint16_t> output(input.width(), input.height(), 3);
    
    halide_blur_cvl(input, output);

    save(output, argv[2]);

    return 0;
}

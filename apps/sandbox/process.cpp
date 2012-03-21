#include <stdio.h>
extern "C" {
#include "local_laplacian.h"
}
#include "../Util.h"
#include "../png.h"
#include <sys/time.h>

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: ./process input.png output.png"
               "e.g. ./process input.png output.png");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint16_t> output(input.width(), input.height(), 3);

    local_laplacian(input, output);
    
    save(output, argv[2]);

    return 0;

}

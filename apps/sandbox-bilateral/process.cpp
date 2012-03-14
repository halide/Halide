#include <stdio.h>
extern "C" {
#include "local_laplacian.h"
}
#include "../Util.h"
#include "../png.h"
#include <sys/time.h>

int main(int argc, char **argv) {

    if (argc < 4) {
        printf("Usage: ./process input.png sigmaSpatial sigmaDomain output.png"
               "e.g. ./process input.png 10 0.2 output.png");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint16_t> output(input.width(), input.height(), 3);
    int k = atoi(argv[2]);
    float sigmaD= atof(argv[3]);

    local_laplacian(sigmaD, k ,input, output);
    
    save(output, argv[4]);

    return 0;

}

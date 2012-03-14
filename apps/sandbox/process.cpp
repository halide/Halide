#include <stdio.h>
extern "C" {
#include "local_laplacian.h"
}
#include "../Util.h"
#include "../png.h"
#include <sys/time.h>

int main(int argc, char **argv) {

    if (argc < 3) {
        printf("Usage: ./filter input.png kernel_radius output.png"
               "e.g. ./filter input.png 10 output.png");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint16_t> output(input.width(), input.height(), 3);
    int k = atoi(argv[2]);
    int sigmaD_100 = atoi(argv[3]);

    local_laplacian(/*sigmaD_100, */k ,input, output);
    
    save(output, argv[4]);

    return 0;

}

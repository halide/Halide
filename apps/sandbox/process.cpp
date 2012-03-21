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

    int nSeams = atoi(argv[2]);

    for (int i=0; i < nSeams; i++) {
      local_laplacian(input, output);
      input = output;
    }
    
    save(output, argv[3]);

    return 0;

}

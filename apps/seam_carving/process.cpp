#include <stdio.h>
extern "C" {
#include "seam_carving.h"
}
#include "../Util.h"
#include "../png.h"
#include <sys/time.h>

int main(int argc, char **argv) {

    if (argc < 2) {
        printf("Usage: ./process input.png NSEAMS output.png\n"
               "e.g. ./process input.png 5 output.png");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);

    int nSeams = atoi(argv[2]);

    Image<uint16_t> output(input.width(), input.height(), 3);
    
    for (int i=0; i < nSeams; i++) {
      printf("."); fflush(stdout);
      seam_carving(input, output);
      input = output;
    }
    
    save(output, argv[3]);

    return 0;

}

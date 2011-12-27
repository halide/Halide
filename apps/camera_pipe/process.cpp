#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

extern "C" {
  #include "curved.h"
}
#include "../Util.h"

int main(int argc, char **argv) {
    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint8_t> output(input.width(), input.height(), 3);

    timeval t1, t2;
    
    gettimeofday(&t1, NULL);
    curved(output.width(), output.height(), 3,
           input.width(), input.height(),
           atof(argv[2]), atof(argv[3]),
           input, output);
    gettimeofday(&t2, NULL);

    printf("%3.3f ms\n", (t2.tv_sec - t1.tv_sec)*1000.0f + (t2.tv_usec - t1.tv_usec)/1000.0f);

    save(output, argv[4]);
    return 0;
}

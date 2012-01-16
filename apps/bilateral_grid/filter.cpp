#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

extern "C" {
  #include "bilateral_grid.h"
}

#include "../Util.h"
#include "../png.h"
using namespace FImage;

int main(int argc, char **argv) {

    Image<float> input = load<float>(argv[1]);
    Image<float> output(input.width(), input.height(), 1);

    timeval t1, t2;

    bilateral_grid(atof(argv[3]), input, output);
    // bilateral_grid(input, output);

    gettimeofday(&t1, NULL);
    for (int i = 0; i < 20; i++) 
        bilateral_grid(atof(argv[3]), input, output);
        // bilateral_grid(input, output);
    gettimeofday(&t2, NULL);

    double t = (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_usec - t1.tv_usec)/1000.0;
    printf("Time: %fms\n", t/20);

    save(output, argv[2]);

    return 0;
}

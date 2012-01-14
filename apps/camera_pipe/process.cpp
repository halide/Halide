#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

extern "C" {
  #include "curved.h"
}
#include "../Util.h"
#include "../png.h"
using namespace FImage;

// void __copy_to_host(buffer_t* b) {}

// TODO: fold into module
extern "C" { typedef struct CUctx_st *CUcontext; }
namespace FImage { CUcontext cuda_ctx = 0; }

int main(int argc, char **argv) {
    Image<uint16_t> input = load<uint16_t>(argv[1]);
    printf("Loaded %dx%dx%d\n", input.width(), input.height(), input.channels());
    Image<uint8_t> output(2560, 1920, 3); // image size is hard-coded for the N900 raw pipeline

    timeval t1, t2;
    
    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    Image<float> matrix_3200 = {{ 1.6697f, -0.2693f, -0.4004f, -42.4346f},
                                {-0.3576f,  1.0615f,  1.5949f, -37.1158f},
                                {-0.2175f, -1.8751f,  6.9640f, -26.6970f}};
    
    Image<float> matrix_7000 = {{ 2.2997f, -0.4478f,  0.1706f, -39.0923f},
                                {-0.3826f,  1.5906f, -0.2080f, -25.4311f},
                                {-0.0888f, -0.7344f,  2.2832f, -20.0826f}};    

    // Warm up run
    // curved(atof(argv[2]), atof(argv[3]), atof(argv[4]),
    //        matrix_3200, matrix_7000, input, output);

    gettimeofday(&t1, NULL);
    curved(atof(argv[2]), atof(argv[3]), atof(argv[4]),
           input, matrix_3200, matrix_7000, output);
    gettimeofday(&t2, NULL);

    printf("%3.3f ms\n", (t2.tv_sec - t1.tv_sec)*1000.0f + (t2.tv_usec - t1.tv_usec)/1000.0f);

    // Current timings on N900 are (best of 10)
    // Halide: 722ms, FCam: 741ms

    save(output, argv[5]);
    // // convert output to 8 bits for easy viewing
    // for (int x = 0; x < output.width(); x++)
    //     for (int y = 0; y < output.height(); y++)
    //         for (int c = 0; c < output.channels(); c++)
    //             fout(x,y,c) = output(x,y,c);
    // save(fout, argv[5]);
    // save(output, "out.16.png");
    
    return 0;
}

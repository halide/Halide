#include <arpa/inet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

extern "C" {
  #include "curved.h"
}
#include <static_image.h>
#include <image_io.h>

#include "fcam/Demosaic.h"
#include "fcam/Demosaic_ARM.h"

int main(int argc, char **argv) {
    if (argc < 6) {
        printf("Usage: ./process raw.png color_temp gamma contrast output.png\n"
               "e.g. ./process raw.png 3200 2 50 output.png");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    Image<uint8_t> output(2560-32, 1920, 3); // image size is hard-coded for the N900 raw pipeline

    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    float _matrix_3200[][4] = {{ 1.6697f, -0.2693f, -0.4004f, -42.4346f},
                                {-0.3576f,  1.0615f,  1.5949f, -37.1158f},
                                {-0.2175f, -1.8751f,  6.9640f, -26.6970f}};

    float _matrix_7000[][4] = {{ 2.2997f, -0.4478f,  0.1706f, -39.0923f},
                                {-0.3826f,  1.5906f, -0.2080f, -25.4311f},
                                {-0.0888f, -0.7344f,  2.2832f, -20.0826f}};
    Image<float> matrix_3200(4, 3), matrix_7000(4, 3);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix_3200(j, i) = _matrix_3200[i][j];
            matrix_7000(j, i) = _matrix_7000[i][j];
        }
    }

    float color_temp = atof(argv[2]);
    float gamma = atof(argv[3]);
    float contrast = atof(argv[4]);

    timeval t1, t2;
    unsigned int bestT = 0xffffffff;
    for (int i = 0; i < 5; i++) {
        gettimeofday(&t1, NULL);
        curved(color_temp, gamma, contrast,
               input, matrix_3200, matrix_7000, output);
        gettimeofday(&t2, NULL);
        unsigned int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
        if (t < bestT) bestT = t;
        if (i % 5 == 0) sleep(1);
    }
    printf("Halide:\t%u\n", bestT);
    save(output, argv[5]);

    bestT = 0xffffffff;
    for (int i = 0; i < 5; i++) {
        gettimeofday(&t1, NULL);
        FCam::demosaic(input, output, color_temp, contrast, true, 25, gamma);
        gettimeofday(&t2, NULL);
        unsigned int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
        if (t < bestT) bestT = t;
        if (i % 5 == 0) sleep(1);
    }
    printf("C++:\t%u\n", bestT);
    save(output, "fcam_c.png");

    bestT = 0xffffffff;
    for (int i = 0; i < 5; i++) {
        gettimeofday(&t1, NULL);
        FCam::demosaic_ARM(input, output, color_temp, contrast, true, 25, gamma);
        gettimeofday(&t2, NULL);
        unsigned int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
        if (t < bestT) bestT = t;
        if (i % 5 == 0) sleep(1);
    }
    printf("ASM:\t%u\n", bestT);
    save(output, "fcam_arm.png");

    // Timings on N900 as of SIGGRAPH 2012 camera ready are (best of 10)
    // Halide: 722ms, FCam: 741ms

    return 0;
}

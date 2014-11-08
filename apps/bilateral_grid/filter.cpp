#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <static_image.h>
#include <image_io.h>

int main(int argc, char **argv) {

    if (argc < 4) {
        printf("Usage: ./filter input.png output.png range_sigma\n"
               "e.g. ./filter input.png output.png 0.1\n");
        return 0;
    }

    Image<float> input = load<float>(argv[1]);
    Image<float> output(input.width(), input.height(), 1);

    bilateral_grid(atof(argv[3]), input, output);

#if 1
    // Timing code
    timeval t1, t2;
    double min_t = 1e10f;
    for (int j = 0; j < 10; j++) {
        gettimeofday(&t1, NULL);
        for (int i = 0; i < 10; i++) {
            bilateral_grid(atof(argv[3]), input, output);
        }
        gettimeofday(&t2, NULL);
        double t = (t2.tv_sec - t1.tv_sec)*1000.0 + (t2.tv_usec - t1.tv_usec)/1000.0;
        if (t < min_t) {
            min_t = t;
        }
    }

    printf("Time: %fms\n", min_t/10);
#endif

    save(output, argv[2]);

    return 0;
}

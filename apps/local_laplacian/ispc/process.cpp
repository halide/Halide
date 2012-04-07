#include <stdio.h>
extern "C" {
#include "local_laplacian.h"
}
#include "../../Util.h"
#include "../../png.h"
#include <sys/time.h>
#include <vector>

void __copy_to_host(buffer_t* buf) {}

using namespace ispc;

int currentTime() {
    static bool initialized = false;
    static timeval start;
    timeval t;
    gettimeofday(&t, NULL);
    if (!initialized) {
        initialized = true;
        start = t;
        return 0;
    }
    int delta = (t.tv_sec - start.tv_sec) * 1000000 + (t.tv_usec - start.tv_usec);
    start = t;
    return delta;
}

void local_laplacian(int levels, float beta, float alpha, Image<uint16_t> input, Image<uint16_t> output) {

    printf("Starting %d\n", currentTime()); fflush(stdout);

    // Convert to floating point
    Image<float> floating(input.width(), input.height(), 3);
    uint16_to_float(input.width(), input.height(), input.data(), floating.data()); 

    // Make the grayscale input
    Image<float> gray(input.width(), input.height());
    rgb2gray(input.width(), input.height(), floating.data(), gray.data());

    // Make the lut
    Image<float> lut(levels*256*2);
    make_remap_lut(levels, alpha, lut.data());

    printf("Make lut %d\n", currentTime()); fflush(stdout);

    // Make the processed base level images
    Image<float> gPyramid[8];
    gPyramid[0] = Image<float>(gray.width(), gray.height(), levels);
    #pragma omp parallel for
    for (int j = 0; j < levels; j++) {
        remap(gray.width(), gray.height(), levels, j, beta,
              gray.data(), lut.data(), gPyramid[0].data());
    }

    printf("Make processed base images %d\n", currentTime()); fflush(stdout);

    // make the processed Gaussian pyramids
    Image<float> scratch(gray.width()*gray.height());
    Image<float> scratch2(gray.width()*gray.height());
    for (int i = 1; i < 8; i++) {
        gPyramid[i] = Image<float>(gPyramid[i-1].width()/2, gPyramid[i-1].height()/2, levels);
        int w = gPyramid[i-1].width(), h = gPyramid[i-1].height();
        #pragma omp parallel for
        for (int j = 0; j < levels; j++) {
            float *gray = &gPyramid[i-1](0, 0, j);
            float *output = &gPyramid[i](0, 0, j);
            downsample_x(w, h, gray, scratch.data());
            downsample_y(w/2, h, scratch.data(), output);
        }
    }

    printf("Make processed Gaussian pyramid %d\n", currentTime()); fflush(stdout);

    // make the gray Gaussian pyramid
    Image<float> inGPyramid[8];
    inGPyramid[0] = gray;
    for (int i = 1; i < 8; i++) {
        int w = inGPyramid[i-1].width(), h = inGPyramid[i-1].height();
        inGPyramid[i] = Image<float>(w/2, h/2);
        downsample_x(w, h, inGPyramid[i-1].data(), scratch.data());
        downsample_y(w/2, h, scratch.data(), inGPyramid[i].data());
    }
    
    printf("Make input Gaussian pyramid %d\n", currentTime()); fflush(stdout);

    // make the gray laplacian pyramid
    Image<float> lPyramid[8];
    lPyramid[7] = gPyramid[7];
    for (int i = 6; i>=0; i--) {
        int w = gPyramid[i].width(), h = gPyramid[i].height();
        lPyramid[i] = Image<float>(w, h, levels);
        #pragma omp parallel for
        for (int j = 0; j < levels; j++) {
            float *lo_gray = &(gPyramid[i+1](0, 0, j));
            float *hi_gray = &(gPyramid[i](0, 0, j));
            float *output = &(lPyramid[i](0, 0, j));
            upsample_x(w/2, h/2, lo_gray, scratch.data());
            upsample_y(w, h/2, scratch.data(), output);            
            rev_subtract_in_place(w, h, hi_gray, output);
        }
    }
    
    printf("Make processed laplacian pyramid %d\n", currentTime()); fflush(stdout);

    // make the output laplacian pyramid   
    Image<float> olPyramid[8];
    for (int i = 0; i < 8; i++) {
        int w = inGPyramid[i].width(), h = inGPyramid[i].height();
        olPyramid[i] = Image<float>(w, h);
        make_output_pyramid(w, h, levels, inGPyramid[i].data(), lPyramid[i].data(), olPyramid[i].data());        
    }

    printf("Make output pyramid %d\n", currentTime()); fflush(stdout);
    
    // collapse output laplacian pyramid
    for (int i = 6; i >= 0; i--) {
        int w = olPyramid[i].width();
        int h = olPyramid[i].height();

        upsample_x(w/2, h/2, &olPyramid[i+1](0, 0), &scratch(0, 0));
        upsample_y(w, h/2, &scratch(0, 0), &scratch2(0, 0));
        add_in_place(w, h, &scratch2(0, 0), &olPyramid[i](0, 0));
    }

    printf("Collapse output pyramid %d\n", currentTime()); fflush(stdout);
    
    // reintroduce color
    Image<float> color(input.width(), input.height(), 3);
    reintroduce_color(input.width(), input.height(), olPyramid[0].data(), gray.data(), floating.data(), color.data());

    printf("Reintroduce color %d\n", currentTime()); fflush(stdout);

    // map output
    float_to_uint16(input.width(), input.height(), color.data(), output.data());

    printf("Mapping output %d\n", currentTime()); fflush(stdout);

    printf("Done %d\n", currentTime());
}

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ./process input.png levels alpha beta output.png\n"
               "e.g.: ./process input.png 8 1 1 output.png\n");
        return 0;
    }

    Image<uint16_t> input = load<uint16_t>(argv[1]);
    int levels = atoi(argv[2]);
    float alpha = atof(argv[3]), beta = atof(argv[4]);    
    Image<uint16_t> output(input.width(), input.height(), 3);

    /* Timing code */
    timeval t1, t2;
    unsigned int bestT = 0xffffffff;
    for (int i = 0; i < 1; i++) {
      gettimeofday(&t1, NULL);
      local_laplacian(levels, beta, alpha/(levels-1), input, output);
      gettimeofday(&t2, NULL);
      unsigned int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
      if (t < bestT) bestT = t;
    }
    printf("%u\n", bestT);

    save(output, argv[5]);

    return 0;
}

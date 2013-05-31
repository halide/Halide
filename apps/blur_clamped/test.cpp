// Compile the halide module like so:
// make -C ../../../FImage/cpp_bindings/ FImage.a && g++-4.6 -std=c++0x halide_blur.cpp -I ../../../FImage/cpp_bindings/ ../../../FImage/cpp_bindings/FImage.a && ./a.out && opt -O3 -always-inline halide_blur.bc | llc -filetype=obj > halide_blur.o

// Then compile this file like so:
// g++-4.6 -Wall -ffast-math -O3 -fopenmp test.cpp halide_blur.o

#include <emmintrin.h>
#include <math.h>
#include <sys/time.h>
#include <stdint.h>

#define cimg_display 0
#include "CImg.h"
using namespace cimg_library;

// TODO: fold into module
extern "C" { typedef struct CUctx_st *CUcontext; }
namespace FImage { CUcontext cuda_ctx = 0; }

timeval t1, t2;
#define begin_timing gettimeofday(&t1, NULL); for (int i = 0; i < 10; i++) {
#define end_timing } gettimeofday(&t2, NULL);

typedef CImg<uint16_t> Image;

extern "C" {
#include "halide_blur.h"
#include "halide_blur_cvl.h"
}

// Convert a CIMG image to a buffer_t for halide
buffer_t halideBufferOfImage(Image &im) {
    buffer_t buf = {0, (uint8_t *)im.data(),
                    {im.width(), im.height(), 3, 1}, 
                    {1, im.width(), im.width()*im.height(), 0}, 
                    {0, 0, 0, 0}, 
                    sizeof(int16_t),
		    false, false};
    return buf;
}

Image blur_halide(Image &in) {
    Image out(in.width(), in.height(), 3);

    buffer_t inbuf = halideBufferOfImage(in);
    buffer_t outbuf = halideBufferOfImage(out);

    // Call it once to initialize the halide runtime stuff
    halide_blur(&inbuf, &outbuf);

    begin_timing;
    
    // Compute the same region of the output as blur_fast (i.e., we're
    // still being sloppy with boundary conditions)
    halide_blur(&inbuf, &outbuf);

    end_timing;

    return out;
}

Image blur_halide_cvl(Image &in) {
    Image out(in.width(), in.height(), 3);

    buffer_t inbuf = halideBufferOfImage(in);
    buffer_t outbuf = halideBufferOfImage(out);

    // Call it once to initialize the halide runtime stuff
    halide_blur_cvl(&inbuf, &outbuf);

    begin_timing;
    
    // Compute the same region of the output as blur_fast (i.e., we're
    // still being sloppy with boundary conditions)
    halide_blur_cvl(&inbuf, &outbuf);

    end_timing;

    return out;
}

int main(int argc, char **argv) {

    Image input(6400, 4800, 3);

    for (int c = 0; c < 3; c++) {
	for (int y = 0; y < input.height(); y++) {
	    for (int x = 0; x < input.width(); x++) {
		input(x, y, c) = rand() & 0xfff;
	    }
	}
    }

    Image halide = blur_halide(input);
    float halide_time = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f;

    Image halide_cvl = blur_halide_cvl(input);
    float halide_cvl_time = (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000000.0f;
    
    printf("times (halide, halide_cvl): %f %f\n", halide_time, halide_cvl_time);
    for (int c = 0; c < 3; c++) {
	for (int y = 0; y < input.height(); y++) {
	    for (int x = 0; x < input.width(); x++) {
		if (halide(x, y, c) != halide_cvl(x, y, c))
		    printf("difference at (%d,%d,%d): %d %d\n", x, y, c,
			   halide(x, y, c), halide_cvl(x, y, c));
	    }
	}
    }
    return 0;
}

/*
The default runner expects:

- to have TEST_FUNC defined to the Halide pipeline name being tested
- to have TEST_IN_T/TEST_OUT_T defined to the type name for the input/output buffer of TEST_FUNC
    e.g. `float` or `uint8_t`
- to have libpng in the include path, and linked into the resulting binary:
    e.g. `$(libpng-config --cflags --ldflags)`
- to be compiled in the same directory with <TEST_FUNC>.h
- to have the Halide/support directory in its include path, for static_image.h/image_io.h
*/

#ifndef TEST_FUNC
#error default_runner must be compiled with TEST_FUNC defined
#endif
#define TEST_HEADER TEST_FUNC.h

#include <Halide.h>
using namespace Halide;
// Utilities to paste a macro as a string constant in another macro.
// via: http://gcc.gnu.org/onlinedocs/cpp/Stringification.html
#define str(s) xstr(s)
#define xstr(s) #s

extern "C" {
#include str(TEST_HEADER)
}
//#include <static_image.h>
#include <image_io.h>
#include <image_equal.h>

#include <sys/time.h>

#include <string>
using std::string;

static const string usage = "Usage:\n\
\trunner <test iterations> <input_image.png> [reference_output.png] [w|-1] [h|-1] [channels|-1] [save_output.png]";

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "%s\n", usage.c_str());
        return -1;
    }

    int test_iterations = atoi(argv[1]);
    
    timeval t1, t2;
    unsigned int t;
    
    Halide::Image<TEST_IN_T> input = load<TEST_IN_T>(argv[2]);
    
    int w        = argc > 4 ? atoi(argv[4]): -1;
    int h        = argc > 5 ? atoi(argv[5]): -1;
    int channels = argc > 6 ? atoi(argv[6]): -1;
    char const *save_output = argc > 7 ? argv[7]: NULL;
    
    if (w < 0) { w = input.width(); }
    if (h < 0) { h = input.height(); }
    if (channels < 0) { channels = input.channels(); }

    Halide::Image<TEST_OUT_T> ref_output(1,1,1);
    bool has_ref = false;
    if (argc > 3 && strcmp(argv[3], "") != 0) {
        ref_output = load<TEST_OUT_T>(argv[3]);
        has_ref = true;
    }

    Halide::Image<TEST_OUT_T> output(1,1,1);

    int selectPadding = 10;
    int iter_outer = 450;

    Halide::Func phi_init("phi_init");
    Halide::Var x("x"), y("y"), c("c");
    phi_init(x,y) = select((x >= selectPadding)
                            && (x < input.width() - selectPadding)
                            && (y >= selectPadding)
                            && (y < input.height() - selectPadding),
                            -2.0f, 2.0f);

    // Timing code
    unsigned int bestT = 0xffffffff;
    for (int i = 0; i < test_iterations; i++) {
        gettimeofday(&t1, NULL);

        Halide::Image<float> phi_buf(phi_init.realize(input.width(), input.height()));
        Halide::Image<float> phi_buf2(input.width(), input.height());

        for(int n = 0 ; n < iter_outer ; ++n){
            TEST_FUNC(Halide::DynImage(input).buffer(), Halide::DynImage(phi_buf).buffer(), Halide::DynImage(phi_buf2).buffer());
            std::swap(phi_buf, phi_buf2);
        }

        Halide::Func masked("masked");

        // Dim the unselected areas for visualization
        masked(x, y, c) = select(phi_buf(x, y) < 0.0f, input(x, y, c), input(x, y, c)/4);
        output = masked.realize(input.width(), input.height(), 3);
        gettimeofday(&t2, NULL);
        t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
        if (t < bestT) bestT = t;
    }

    // Saving large PNGs is expensive. Only do it if enabled.
    if (save_output != NULL && strcmp(save_output, "") != 0) {
        save(output, save_output);
    }

    if (has_ref) {
        if (!images_equal<TEST_OUT_T>(ref_output, output, 0.01)) {
            printf("RUN_CHECK_FAIL\n");
            exit(1);
        }
    }
    printf("Success %f\n", bestT/1000000.0);

    return 0;
}

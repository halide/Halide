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

// Utilities to paste a macro as a string constant in another macro.
// via: http://gcc.gnu.org/onlinedocs/cpp/Stringification.html
#define str(s) xstr(s)
#define xstr(s) #s

extern "C" {
#include str(TEST_HEADER)
}
#include <static_image.h>
#include <image_io.h>

#include <sys/time.h>

#include <string>
using std::string;

static const string usage = "Usage:\n\
\trunner <test iterations> <input_image.png>";

int main(int argc, char const *argv[])
{
	if (argc != 3) {
		fprintf(stderr, "%s\n", usage.c_str());
		return -1;
	}

    int test_iterations = atoi(argv[1]);

	Image<TEST_IN_T> input = load<TEST_IN_T>(argv[2]);
	Image<TEST_OUT_T> output(input.width(), input.height(), input.channels());

    // Timing code
    timeval t1, t2;
    unsigned int bestT = 0xffffffff;
    for (int i = 0; i < test_iterations; i++) {
        gettimeofday(&t1, NULL);
        TEST_FUNC(input, output);
        gettimeofday(&t2, NULL);
        unsigned int t = (t2.tv_sec - t1.tv_sec) * 1000000 + (t2.tv_usec - t1.tv_usec);
        if (t < bestT) bestT = t;
    }
    printf("%f\n", bestT/1000000.0);

    save(output, "out.png");

	return 0;
}

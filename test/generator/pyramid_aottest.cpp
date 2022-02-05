#include <math.h>
#include <stdio.h>

#include "HalideBuffer.h"
#include "pyramid.h"

#include <vector>
using std::vector;
using namespace Halide::Runtime;

int main(int argc, char **argv) {
    Buffer<float, 2> input(1024, 1024);

    // Put some junk in the input. Keep it to small integers so the float averaging stays exact.
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            input(x, y) = (float)(((x * 17 + y) / 8) % 32);
        }
    }

    vector<Buffer<float, 2>> levels(10);

    for (int l = 0; l < 10; l++) {
        levels[l] = Buffer<float, 2>(1024 >> l, 1024 >> l);
    }

    // Will throw a compiler error if we didn't compile the generator with 10 levels.
    pyramid(input,
            levels[0], levels[1], levels[2], levels[3], levels[4],
            levels[5], levels[6], levels[7], levels[8], levels[9]);

    // The bottom level should be the input
    for (int y = 0; y < input.height(); y++) {
        for (int x = 0; x < input.width(); x++) {
            if (input(x, y) != levels[0](x, y)) {
                printf("input(%d, %d) = %f, but levels[0](%d, %d) = %f\n",
                       x, y, input(x, y), x, y, levels[0](x, y));
                return -1;
            }
        }
    }

    // The remaining levels should be averaging of the levels above them.
    for (int l = 1; l < 10; l++) {
        for (int y = 0; y < (input.height() >> l); y++) {
            for (int x = 0; x < (input.width() >> l); x++) {
                float correct = (levels[l - 1](2 * x, 2 * y) +
                                 levels[l - 1](2 * x + 1, 2 * y) +
                                 levels[l - 1](2 * x, 2 * y + 1) +
                                 levels[l - 1](2 * x + 1, 2 * y + 1)) /
                                4;
                float actual = levels[l](x, y);
                if (correct != actual) {
                    printf("levels[%d](%d, %d) = %f instead of %f\n",
                           l, x, y, actual, correct);
                    return -1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

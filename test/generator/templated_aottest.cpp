#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <math.h>
#include <stdio.h>

#include "templated.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    const int kSize = 1024;

    Buffer<double, 2> output(kSize, kSize);
    Buffer<float, 2> input(kSize, kSize);

    input.fill(17.0f);

    templated(input, output);

    auto check = [&](float val, float input_val) {
        if (val != input_val + 2) {
            printf("Output value was %f instead of %f\n",
                   val, input_val + 2);
            exit(1);
        }
    };

    output.for_each_value(check, input);

    printf("Success!\n");
    return 0;
}

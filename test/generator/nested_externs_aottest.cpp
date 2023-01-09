#include <stdio.h>
#include <stdlib.h>

#include "HalideBuffer.h"
#include "nested_externs_root.h"

using namespace Halide::Runtime;

int main(int argc, char **argv) {
    auto buf = Buffer<float, 3>::make_interleaved(100, 200, 3);
    auto val = Buffer<float, 0>::make_scalar();
    val() = 38.5f;

    nested_externs_root(val, buf);

    buf.for_each_element([&](int x, int y, int c) {
        const float correct = 158.0f;
        const float actual = buf(x, y, c);
        if (actual != correct) {
            printf("result(%d, %d, %d) = %f instead of %f\n",
                   x, y, c, actual, correct);
            exit(-1);
        }
    });

    printf("Success!\n");
    return 0;
}

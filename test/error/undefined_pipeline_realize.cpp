#include "Halide.h"
#include <assert.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");

    Pipeline p(f);
    Buffer<int32_t> result = p.realize(100, 5, 3);

    // We shouldn't reach here, because there should have been a compile error.
    printf("There should have been an error\n");

    return 0;
}

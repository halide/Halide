#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    const Buffer<> foo;
    foo.raw_buffer();

    printf("Success!\n");
    return 0;
}

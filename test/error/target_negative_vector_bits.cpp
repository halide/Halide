#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t(Target::Linux, Target::ARM, 64, {Target::SVE2});
    // vector_bits must be non-negative.
    t.set_vector_bits(-128);

    printf("Success!\n");
    return 0;
}

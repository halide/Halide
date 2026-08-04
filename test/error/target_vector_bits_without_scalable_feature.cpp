#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // vector_bits is only meaningful with a scalable-vector feature.
    Target t("x86-64-linux-vector_bits_128");

    printf("Success!\n");
    return 0;
}

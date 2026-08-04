#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // A cuda capability is meaningless without the cuda feature.
    Target t("x86-64-linux-cuda_capability_50");

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // sve2 is an ARM feature; it is not valid on x86.
    Target t("x86-64-linux-sve2");

    printf("Success!\n");
    return 0;
}

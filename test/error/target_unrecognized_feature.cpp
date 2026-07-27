#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // "frobnicate" is not a known target token.
    Target t("x86-64-linux-frobnicate");

    printf("Success!\n");
    return 0;
}

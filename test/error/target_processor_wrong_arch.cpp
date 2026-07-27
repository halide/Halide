#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // tune_znver4 is an x86 processor; it is not valid on ARM.
    Target t("arm-64-linux-tune_znver4");

    printf("Success!\n");
    return 0;
}

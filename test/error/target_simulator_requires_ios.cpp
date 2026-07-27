#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // The simulator feature is only valid for iOS.
    Target t("x86-64-linux-simulator");

    printf("Success!\n");
    return 0;
}

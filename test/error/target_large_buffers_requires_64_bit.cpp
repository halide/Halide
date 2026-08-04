#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // large_buffers needs 64-bit indexing.
    Target t("x86-32-linux-large_buffers");

    printf("Success!\n");
    return 0;
}

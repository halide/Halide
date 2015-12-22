#include "Halide.h"
#include <stdio.h>

using namespace Halide;
int main(int argc, char **argv) {
    if (sizeof(void *) == 8) {
        Buffer result(UInt(8), 1 << 24, 1 << 24, 1 << 24);
    } else {
        Buffer result(UInt(8), 1 << 12, 1 << 12, 1 << 8);
    }
    printf("Success!\n");
}

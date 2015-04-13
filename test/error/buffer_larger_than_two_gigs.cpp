#include "Halide.h"
#include <stdio.h>

using namespace Halide;
int main(int argc, char **argv) {
    Buffer result(UInt(8), 4096, 4096, 256);
    printf("Success!\n");
}

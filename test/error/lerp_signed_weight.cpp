#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // This should trigger an error.
    Func f;
    f() = lerp(cast<uint8_t>(0), cast<uint8_t>(42), cast<int8_t>(16));

    printf("Success!\n");
    return 0;
}

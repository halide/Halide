#include "Halide.h"

#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // This should trigger an error.
    Func f;
    f() = lerp(cast<uint16_t>(0), cast<uint8_t>(42), 0.5f);

    printf("Success!\n");
    return 0;
}

#include <stdio.h>
#include <type_traits>

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This should trigger an error.
    Func f;
    f() = lerp(0, 42, 1.5f);

    printf("Success!\n");
    return 0;
}

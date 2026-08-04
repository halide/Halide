#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t(Target::Linux, Target::X86, 64);
    // Only 0, 32, and 64 are valid bit widths.
    t.set_bits(17);

    printf("Success!\n");
    return 0;
}

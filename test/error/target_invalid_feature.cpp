#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t(Target::Linux, Target::X86, 64);
    // Setting an out-of-range feature is an error.
    t.set_feature((Target::Feature)10000);

    printf("Success!\n");
    return 0;
}

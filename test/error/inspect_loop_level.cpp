#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    LoopLevel root = LoopLevel::root();

    printf("LoopLevel is %s\n", root.to_string().c_str()); // should fail

    printf("I should not have reached here\n");

    return 0;
}

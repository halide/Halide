#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    const Buffer<> foo;
    foo.raw_buffer();

    printf("I should not have reached here\n");
    return 0;
}

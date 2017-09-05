#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;
int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    if (sizeof(void *) == 8) {
        Buffer<uint8_t> result(1 << 24, 1 << 24, 1 << 24);
    } else {
        Buffer<uint8_t> result(1 << 12, 1 << 12, 1 << 8);
    }
    printf("Success!\n");
}

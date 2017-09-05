#include <assert.h>
#include <stdio.h>
#include "test/common/expect_death.h"
#include "Halide.h"

#include "test/common/halide_test_dirs.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Func f("f");

    Buffer<int32_t> result = f.realize(100, 5, 3);

    // We shouldn't reach here, because there should have been a compile error.
    printf("There should have been an error\n");

    return 0;
}

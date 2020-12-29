#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");

    Buffer<int32_t> result = f.realize(100, 5, 3);

    // We shouldn't reach here, because there should have been a compile error.
    printf("Success!\n");
    return 0;
}

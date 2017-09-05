#include <stdio.h>
#include "test/common/expect_death.h"
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Var x;
    Func f;
    RDom dom(0, 50);

    f(x) = cast<uint8_t>(0); // The type here...
    f(dom) += 1.0f;          // does not match the type here.

    // Should result in an error
    Buffer<float> result = f.realize(50);

    printf("Success!\n");
    return 0;
}

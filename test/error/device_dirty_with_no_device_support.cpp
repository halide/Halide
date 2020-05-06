#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<float> im(128, 128);

    Func f;
    Var x, y;
    f(x, y) = im(x, y);

    im.set_device_dirty(true);

    // Explicitly don't use device support
    f.realize(128, 128, Target{"host"});

    printf("Should have failed\n");
    return 0;
}

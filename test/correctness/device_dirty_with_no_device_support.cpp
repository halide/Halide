#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("device_dirty_with_no_device_support", "is dirty on device, but this pipeline was compiled with no support for device to host copies.", []() {
        Buffer<float> im(128, 128);

        Func f;
        Var x, y;
        f(x, y) = im(x, y);

        im.set_device_dirty(true);

        // Explicitly don't use device support
        f.realize({128, 128}, Target{"host"});
    });
}

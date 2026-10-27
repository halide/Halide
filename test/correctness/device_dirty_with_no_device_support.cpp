#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Under the SERIALIZATION_JIT_ROUNDTRIP_TESTING build configuration, the
    // pipeline is serialized/deserialized before running, which rejects the
    // device-dirty buffer earlier (and with a different message) than the
    // "no device to host copies" check this test otherwise exercises.
    return error_test("device_dirty_with_no_device_support",
                      {"is dirty on device, but this pipeline was compiled with no support for device to host copies.",
                       "Cannot serialize on-device buffer"},
                      []() {
                          Buffer<float> im(128, 128);

                          Func f;
                          Var x, y;
                          f(x, y) = im(x, y);

                          im.set_device_dirty(true);

                          // Explicitly don't use device support
                          f.realize({128, 128}, Target{"host"});
                      });
}

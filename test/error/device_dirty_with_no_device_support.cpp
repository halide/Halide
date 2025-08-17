#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestDeviceDirtyWithNoDeviceSupport() {
    Buffer<float> im(128, 128);

    Func f;
    Var x, y;
    f(x, y) = im(x, y);

    im.set_device_dirty(true);

    // Explicitly don't use device support
    f.realize({128, 128}, Target{"host"});
}
}  // namespace

TEST(ErrorTests, DeviceDirtyWithNoDeviceSupport) {
    EXPECT_RUNTIME_ERROR(TestDeviceDirtyWithNoDeviceSupport, HasSubstr("TODO"));
}

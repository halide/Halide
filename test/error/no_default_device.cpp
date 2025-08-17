#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestNoDefaultDevice() {
    Target t("host");
    (void)get_device_interface_for_device_api(DeviceAPI::Default_GPU, t, "No Default Device Test");
}
}  // namespace

TEST(ErrorTests, NoDefaultDevice) {
    EXPECT_COMPILE_ERROR(TestNoDefaultDevice, HasSubstr("TODO"));
}

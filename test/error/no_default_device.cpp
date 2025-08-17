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
    EXPECT_COMPILE_ERROR(TestNoDefaultDevice, MatchesPattern(R"(get_device_interface_for_device_api called from No Default Device Test requested a default GPU but no GPU feature is specified in target \(arm-64-osx-arm_dot_prod-arm_fp\d+\)\.)"));
}

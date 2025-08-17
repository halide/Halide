#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestBadDeviceApi() {
    Target t("host");
    (void)get_device_interface_for_device_api((DeviceAPI)-1, t, "Bad DeviceAPI");
}
}  // namespace

TEST(ErrorTests, BadDeviceApi) {
    EXPECT_COMPILE_ERROR(TestBadDeviceApi, MatchesPattern(R"(get_device_interface_for_device_api called from Bad DeviceAPI requested unknown DeviceAPI \(-1\)\.)"));
}

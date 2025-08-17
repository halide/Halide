#include "Halide.h"
#include "halide_test_error.h"

using namespace Halide;

namespace {
void TestDeviceTargetMismatch() {
    Target t("host");
    (void)get_device_interface_for_device_api(DeviceAPI::CUDA, t, "Device Target Mistmatch Test");
}
}  // namespace

TEST(ErrorTests, DeviceTargetMismatch) {
    EXPECT_COMPILE_ERROR(
        TestDeviceTargetMismatch,
        MatchesPattern(R"(get_device_interface_for_device_api called from Device )"
                       R"(Target Mistmatch Test DeviceAPI \(cuda\) is not supported )"
                       R"(by target \(.+\)\.)"));
}

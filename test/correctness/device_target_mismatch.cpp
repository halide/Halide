#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("device_target_mismatch", "get_device_interface_for_device_api called from Device Target Mismatch Test DeviceAPI (cuda) is not supported by target (", []() {
        Target t("host");
        (void)get_device_interface_for_device_api(DeviceAPI::CUDA, t, "Device Target Mismatch Test");
    });
}

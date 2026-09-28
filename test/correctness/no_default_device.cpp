#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    return error_test("no_default_device", "get_device_interface_for_device_api called from No Default Device Test requested a default GPU but no GPU feature is specified in target (", []() {
        Target t("host");
        (void)get_device_interface_for_device_api(DeviceAPI::Default_GPU, t, "No Default Device Test");
    });
}

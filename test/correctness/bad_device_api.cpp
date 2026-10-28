#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("bad_device_api", "get_device_interface_for_device_api called from Bad DeviceAPI requested unknown DeviceAPI (-1).", []() {
        Target t("host");
        (void)get_device_interface_for_device_api((DeviceAPI)-1, t, "Bad DeviceAPI");
    });
}

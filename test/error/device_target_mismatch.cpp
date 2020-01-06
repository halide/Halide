#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t("host");
    (void)get_device_interface_for_device_api(DeviceAPI::CUDA, t, "Device Target Mistmatch Test");

    printf("I should not have reached here\n");

    return 0;
}

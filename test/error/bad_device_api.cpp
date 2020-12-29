#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t("host");
    (void)get_device_interface_for_device_api((DeviceAPI)-1, t, "Bad DeviceAPI");

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // profile and profile_by_timer are mutually exclusive.
    Target t("x86-64-linux-profile-profile_by_timer");

    printf("Success!\n");
    return 0;
}

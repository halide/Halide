#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // natural_vector_size on a hexagon target without hvx is an error.
    Target t("hexagon-32-qurt");
    (void)t.natural_vector_size<int32_t>();

    printf("Success!\n");
    return 0;
}

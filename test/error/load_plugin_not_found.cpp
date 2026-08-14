#include "Halide.h"

#include <cstdio>

int main(int argc, char **argv) {
    Halide::load_plugin("definitely_does_not_exist_xyz123");

    printf("Success!\n");
    return 0;
}

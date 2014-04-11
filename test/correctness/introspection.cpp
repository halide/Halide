#include <Halide.h>
#include <stdio.h>

int main(int argc, char **argv) {
    bool result = HalideIntrospectionCanary::test();

    if (result) {
        printf("Halide C++ introspection seems to be working with this build config\n");
    } else {
        printf("Halide C++ introspection doesn't claim to work with this build config. Not continuing.\n");
        return 0;
    }

    printf("Continuing with further tests...\n");

    return 0;
}

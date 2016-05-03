#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t;

    // Calling natural_vector_size() on an uninitialized / default Target
    // should generate user_error.
    (void) t.natural_vector_size<float>();

    printf("I should not have reached here\n");
    return 0;
}

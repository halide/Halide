#include "Halide.h"
#include <stdio.h>
#include "test/common/expect_death.h"

using namespace Halide;

int main(int argc, char **argv) {
    HALIDE_EXPECT_DEATH(argc, argv);

    Target t;

    // Calling natural_vector_size() on a Target with Unknown fields
    // should generate user_error.
    (void) t.natural_vector_size<float>();

    printf("I should not have reached here\n");
    return 0;
}

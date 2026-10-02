#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("unknown_target", "natural_vector_size cannot be used on a Target with Unknown values.", []() {
        Target t;

        // Calling natural_vector_size() on a Target with Unknown fields
        // should generate user_error.
        (void)t.natural_vector_size<float>();
    });
}

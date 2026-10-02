#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("impossible_constraints", "Inferring input bounds on Pipeline didn't converge after 16 iterations. There may be unsatisfiable constraints\n", []() {
        ImageParam input(Float(32), 2, "in");

        Func out("out");

        // The requires that the input be larger than the input
        out() = input(input.width(), input.height()) + input(0, 0);

        out.infer_input_bounds({});
    });
}

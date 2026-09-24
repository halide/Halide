#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("define_after_use", "Func f cannot be given a new update definition, because it has already been realized, used in the definition of another Func, or been the target of a wrapper via in()/clone_in().", []() {
        Func f, g;
        Var x;

        f(x) = x;
        g(x) = f(x) + 1;

        // Now try to add an update definition to f
        f(x) += 1;
    });
}

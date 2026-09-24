#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("wrong_dimensionality_extern_stage", "Func \"f1\" was called with 2 arguments, but was defined with 3", []() {
        Func f, g;
        Var x, y;

        g.define_extern("foo", {}, UInt(16), 3);

        // Show throw an error immediately because g was defined with 3 dimensions.
        f(x, y) = cast<float>(g(x, y));
    });
}

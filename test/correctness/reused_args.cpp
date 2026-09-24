#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("reused_args", "arguments 1 and 0 both have the name \"v0\"", []() {
        Func f;
        Var x;
        // You can't use the same variable more than once in the LHS of a
        // pure definition.
        f(x, x) = x;
    });
}

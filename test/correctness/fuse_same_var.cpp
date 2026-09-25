#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

int main(int argc, char **argv) {
    return error_test("fuse_same_var", "In schedule for f0, inner and outer fuse dimensions must be distinct, both are: v0", []() {
        Func f;
        Var x, y, fused;

        f(x, y) = x + y;
        f.fuse(x, x, fused);
    });
}

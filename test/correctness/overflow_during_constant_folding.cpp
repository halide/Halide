#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("overflow_during_constant_folding", "Signed integer overflow occurred during constant-folding. Signed integer overflow for int32 and int64 is undefined behavior in Halide.", []() {
        Func f;
        Var x;
        f(x) = Expr(0x12345678) * Expr(0x76543210);

        f.realize({10});
    });
}

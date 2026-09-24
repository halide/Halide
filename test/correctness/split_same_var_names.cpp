#include "Halide.h"
#include "expect_user_error.h"

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("split_same_var_names", "can't split x into x and x because the new Vars have the same name.", []() {
        Var x;
        Func f;
        f(x) = x;
        f.split(x, x, x, 16, TailStrategy::RoundUp);
    });
}

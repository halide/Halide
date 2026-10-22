#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("wrap_frozen", "Func f_in_g$0 cannot be given a new update definition, because it has already "
                                     "been realized, used in the definition of another Func, or been the target "
                                     "of a wrapper via in()/clone_in().",
                      []() {
                          Func f("f"), g("g");
                          Var x("x"), y("y");

                          f(x) = x;
                          g(x) = f(x);
                          Func wrapper = f.in(g);
                          wrapper(x) += 1;
                      });
}

#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("undefined_loop_level", "There should be no undefined LoopLevels at the start of lowering. "
                                              "(Did you mean to use LoopLevel::inlined() instead of LoopLevel() ?)",
                      []() {
                          LoopLevel undefined;

                          Var x;
                          Func f, g;
                          f(x) = x;
                          g(x) = f(x);
                          f.compute_at(undefined);
                          g.compute_root();

                          // Trying to lower/realize with an undefined LoopLevel should be fatal
                          Buffer<int> result = g.realize({1});
                          (void)result;
                      });
}

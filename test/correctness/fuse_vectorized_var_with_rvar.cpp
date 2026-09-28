#include "Halide.h"
#include "expect_user_error.h"
using namespace Halide;

// From https://github.com/halide/Halide/issues/7871

int main() {
    return error_test("fuse_vectorized_var_with_rvar", "In schedule for local_sum.update(0), marking var r0 as parallel or "
                                                       "vectorized may introduce a race condition resulting in incorrect output.",
                      []() {
                          Func input("input");
                          Func local_sum("local_sum");
                          Func blurry("blurry");
                          Var x("x"), y("y");
                          RVar yryf;
                          input(x, y) = 2 * x + 5 * y;
                          RDom r(-2, 5, -2, 5, "rdom_r");
                          local_sum(x, y) = 0;
                          local_sum(x, y) += input(x + r.x, y + r.y);
                          blurry(x, y) = cast<int32_t>(local_sum(x, y) / 25);

                          // Should throw an error because we're trying to fuse a vectorized Var with
                          // an impure RVar.
                          local_sum.update(0).vectorize(y).fuse(y, r.y, yryf);
                      });
}

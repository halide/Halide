#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("fold_storage_not_applied", "Explicit storage folding of Func f0 along dimension v0 with fold factor 8 "
                                                  "was requested via fold_storage(), but storage folding did not attempt "
                                                  "to fold that dimension.",
                      []() {
                          Var x, y;

                          Func f, g;

                          f(x, y) = x + y;
                          g(x, y) = f(x, y) + f(x, y - 1);

                          f.store_root().compute_at(g, y).fold_storage(x, 8);

                          Buffer<int> im = g.realize({10, 10});
                          (void)im;
                      });
}

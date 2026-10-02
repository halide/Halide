#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("host_inside_gpu_loop", "The host() schedule directive cannot be used inside a <Default_GPU> loop. It is currently only supported "
                                              "to leave an enclosing sme_streaming() loop or in host loop redundantly.\n",
                      []() {
                          Func f("f");
                          Func g("g");
                          Var x("x"), xo("xo"), xi("xi");

                          f(x) = x * 0.1f;
                          g(x) = f(x) * f(x);

                          g.compute_root().gpu_tile(x, xo, xi, 256);
                          f.compute_at(g, xo).host();

                          g.compile_jit(Target{"host-opencl"});
                      });
}

#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("compute_with_fuse_in_specialization", "Invalid compute_with: cannot find x in out0.s0", []() {
        Var x("x"), y("y"), f("f");
        ImageParam in(Int(16), 2, "in");
        Func out0("out0"), out1("out1");
        out0(x, y) = 1 * in(x, y);
        out1(x, y) = 2 * in(x, y);

        out0.vectorize(x, 8, TailStrategy::RoundUp);
        out1.vectorize(x, 8, TailStrategy::RoundUp).compute_with(out0, x);

        out0.specialize(in.dim(1).stride() == 128).fuse(x, y, f);
        Pipeline p({out0, out1});
        p.compile_jit();
    });
}

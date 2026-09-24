#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::CUDA);
    return error_test("thread_id_outside_block_id", "must be inside a GPU block loop", [&]() {
        Func f;
        Var x;
        f(x) = x;
        Var xo, xi;
        f.gpu_tile(x, xo, xi, 16).reorder(xo, xi);

        f.compile_jit(t);
        Buffer<int> result = f.realize({16});
        (void)result;
    });
}

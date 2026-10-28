#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("memoize_different_compute_store", "cannot be memoized because it has compute and storage scheduled at different loop levels.", []() {
        Param<float> val;

        Func f, g;
        Var x, y;

        f(x, y) = val + cast<uint8_t>(x);
        g(x, y) = f(x, y) + f(x - 1, y) + f(x + 1, y);

        g.split(y, y, _, 16);
        f.store_root();
        f.compute_at(g, y).memoize();

        val.set(23.0f);
        Buffer<uint8_t> out = g.realize({128, 128});
        (void)out;
    });
}

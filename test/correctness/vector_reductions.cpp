#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g, c;
    Var x;

    c(x) = cast<uint8_t>(x);
    f(x) = cast<uint8_t>(x);
    f.compute_root();
    c.compute_root();

    /*
    RDom r(0, 16);

    g(x) += cast<uint16_t>(f(x + r)) * c(r);
    */

    RDom r(0, 4);
    g(x) += cast<uint16_t>(f(4 * x + r));  // * c(r);

    Var xo, xi;
    RVar rx;
    g.bound(x, 0, 128).update().atomic().split(x, xo, xi, 32).fuse(r, xi, rx).vectorize(rx);

    g.compile_to_assembly("/dev/stdout", {}, Target("arm-64-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));

    return 0;
}

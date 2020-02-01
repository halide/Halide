#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // TO test: min/max/add/dot/mul for lots of src/dst type pairs

    // Fused combinations of x and a reduction at various combos

    Func f, g, c;
    Var x, y;

    c(x) = cast<int16_t>(x);
    f(y, x) = cast<int16_t>(x);
    f.compute_root();
    c.compute_root();

    RDom r(0, 16);

    g(x) += cast<int32_t>(f(r, x)) * c(r);

    Var xo, xi;
    RVar rx;
    g.bound(x, 0, 128).update().atomic().split(x, xo, xi, 1).fuse(r, xi, rx).vectorize(rx);

    //g.compile_to_assembly("/dev/stdout", {}, Target("arm-64-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));
    //g.compile_to_assembly("/dev/stdout", {}, Target("arm-32-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));
    g.compile_to_assembly("/dev/stdout", {}, Target("host-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_opt"));

    return 0;
}

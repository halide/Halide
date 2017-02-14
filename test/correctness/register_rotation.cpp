#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f"), g("g");
    Var x, xi;

    g(x) = sqrt(x);
    f(x) = g(x) + g(x+8);

    g.store_root().compute_at(f, xi).fold_storage(x, 32).vectorize(x, 8, TailStrategy::RoundUp);//.unroll(x);
    f.bound(x, 0, 1024).vectorize(x, 8).split(x, x, xi, 4, TailStrategy::RoundUp).unroll(xi);

    f.compile_to_assembly("/dev/stdout", {}, Target("host-no_runtime-no_bounds_query-no_asserts"));

    f.realize(1024);

    return 0;
}

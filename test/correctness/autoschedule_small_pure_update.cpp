#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<float> in(13, 17);
    ImageParam in_param(Float(32), 2);

    Func g, h;
    Var x, y;

    RDom r(0, 17);
    g(x) += in_param(x, r);

    h(x, y) = in_param(x, y) + g(x);

    h.set_estimates({{0, 13}, {0, 17}});
    in_param.set_estimates({{0, 13}, {0, 17}});

    Pipeline p(h);
    p.auto_schedule(Target("host"));

    in_param.set(in);

    // Ensure the autoscheduler doesn't try to RoundUp the pure loop
    // in g's update definition.
    p.realize(13, 17);

    return 0;
}

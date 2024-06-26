#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] Autoschedulers do not support WebAssembly.\n");
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <autoscheduler-lib>\n", argv[0]);
        return 1;
    }

    load_plugin(argv[1]);

    Buffer<float> in(13, 17);
    ImageParam in_param(Float(32), 2);

    Func g, h;
    Var x, y;

    RDom r(0, 17);
    g(x) += in_param(x, r);

    h(x, y) = in_param(x, y) + g(x);

    h.set_estimates({{0, 13}, {0, 17}});
    in_param.set_estimates({{0, 13}, {0, 17}});

    Target target = get_target_from_environment();
    Pipeline p(h);
    p.apply_autoscheduler(target, {"Mullapudi2016"});

    in_param.set(in);

    // Ensure the autoscheduler doesn't try to RoundUp the pure loop
    // in g's update definition.
    p.realize({13, 17});

    printf("Success!\n");
    return 0;
}

#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // This mirrors test/correctness/gpu_assertion_in_kernel.cpp: unrolling a
    // bounded Var that is compute_at'd inside a GPU loop causes a
    // bounds-safety assertion to be emitted inside the device kernel body.
    //
    // Assertions cannot be represented inside a Metal kernel, so the Metal
    // backend should warn and drop the assertion (like the OpenCL backend
    // does), instead of emitting Metal source that fails to compile.
    Func f, g;
    Var c, x, xi;
    f(c, x) = x + c + 3;
    f.bound(c, 0, 3).unroll(c);

    g(c, x) = f(c, x) * 8;

    g.gpu_tile(x, xi, 8);
    f.compute_at(g, x).gpu_threads(x);

    Target t = get_jit_target_from_environment();
    t.set_feature(Target::Metal);

    g.compile_to_module(g.infer_arguments(), "g", t);

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Make sure that freeing GPU buffers doesn't occur before the
    // computation that is filling them completes.
    Func f;
    Var x, y, xi, yi;
    RDom r(0, 100);
    f(x, y) = sum(sqrt(sqrt(sqrt(sqrt(x + y + r)))));

    Target t = get_jit_target_from_environment();

    if (t.has_feature(Target::OpenCL) ||
        t.has_feature(Target::CUDA) ||
        t.has_feature(Target::WebGPU)) {
        f.gpu_tile(x, y, xi, yi, 16, 16);

        // This allocates a buffer, does gpu compute into it, and then
        // frees it (calling dev_free) possibly before the compute is
        // done.
        for (int i = 0; i < 10; i++) {
            f.realize({1024, 1024}, t);
        }
    } else {
        // Skip this test if gpu target not enabled (it's pretty slow on a cpu).
    }

    printf("Success!\n");
    return 0;
}

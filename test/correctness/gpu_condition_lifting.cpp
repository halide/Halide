#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    // See https://github.com/halide/Halide/issues/4297
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        return 0;
    }
    Var x, y, z;
    Func f;
    f(x, y, z) = 0;
    Var yo, yi;
    f.split(y, yo, yi, 32, TailStrategy::GuardWithIf)
        .reorder(x, z, yi, yo)
        .gpu_blocks(yo)
        .gpu_blocks(yi)
        .gpu_blocks(z);

    Buffer<int> imf = f.realize(10, 10, 10, target);
    return 0;
}

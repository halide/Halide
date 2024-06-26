#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f, g;
    Var x, y;

    f(x, y) = x + y;
    g(x, y) = f(x, y);

    Var xi, yi, xii, yii;
    g.compute_root()
        .gpu_tile(x, y, xi, yi, 64, 16, TailStrategy::GuardWithIf)
        .tile(xi, yi, xii, yii, 2, 2)
        .unroll(xii)
        .unroll(yii);

    f.compute_at(g, xi)
        .store_in(MemoryType::Register)
        .unroll(x)
        .unroll(y);

    // This tests two things

    // 1) Because of the GuardWithIf on g, we need a variable amount
    // of f. If you put it in registers it should take an upper bound
    // on the size required. It should also be possible to unroll it
    // entirely by injecting if statements.

    // 2) No other test uses MemoryType::Register without also having
    // a GPULanes loop. This used to break (the allocation would
    // disappear entirely).

    Buffer<int> result = g.realize({123, 245});

    for (int y = 0; y < result.height(); y++) {
        for (int x = 0; x < result.width(); x++) {
            int correct = x + y;
            if (result(x, y) != correct) {
                printf("result(%d, %d) = %d instead of %d\n",
                       x, y, result(x, y), correct);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

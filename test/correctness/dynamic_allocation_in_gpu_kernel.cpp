#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target t(get_jit_target_from_environment());
    if (!t.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Func f1, f2, f3, f4, f5, f6, g;
    Var x, y;
    Param<int> p;

    f1(x, y) = cast<float>(x + y);
    f2(x, y) = cast<int>(f1(x, y) + f1(x + 1, y + 1));
    f3(x, y) = cast<float>(f2(x, y) + f2(x + 1, y + 1));
    f4(x, y) = cast<int>(f3(x, y) + f3(x + 1, y + 1));
    f5(x, y) = cast<int>(f4(x, y) + f4(x + 1, y + 1));
    f6(x, y) = cast<float>(f5(x, y) + f5(x + 1, y + 1));
    g(x, y) = f6(x, y) + f6(x + p, y + p);

    // All of the f's have a dynamic size required (it depends on p),
    // so we'll store them in global memory ("Heap"). On cuda we get
    // one big heap allocation. On d3d we should get one
    // allocation per coalesced group, and groups can only be
    // coalesced if the types match, so we get an allocation for
    // [f1,f3,f6], another for [f2,f4], and a third for f5.

    Var xi, yi;
    g.gpu_tile(x, y, xi, yi, 16, 8);
    f1.compute_at(g, x).store_in(MemoryType::Heap);
    f2.compute_at(g, x).store_in(MemoryType::Heap);
    f3.compute_at(g, x).store_in(MemoryType::Heap);
    f4.compute_at(g, x).store_in(MemoryType::Heap);
    f5.compute_at(g, x).store_in(MemoryType::Heap);
    f6.compute_at(g, xi);

    constexpr int W = 128, H = 128;

    for (int i = 0; i < 10; i++) {
        p.set(i);
        Buffer<float> result = g.realize({W, H});
        result.copy_to_host();
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float correct = 64 * x + 64 * y + 64 * i + 320;
                float actual = result(x, y);
                if (correct != actual) {
                    printf("result[%d](%d, %d) = %f instead of %f\n",
                           i, x, y, actual, correct);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

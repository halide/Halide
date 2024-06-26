#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Var x, y;
    Func f, g;

    // Up to about 40MB/image * 2 buffers seems to work on luxosr, when freshly booted
    // 130MB works on 2GB Quadro 4000 when freshly booted

    // Here we'll allocated 10MB/image * 2 buffers, so that the test passes reliably.
    int W = 1024 * 10 / 4, H = 1024;

    printf("Defining function...\n");

    f(x, y) = max(x, y);
    g(x, y) = clamp(f(x, y), 20, 100);

    Target target = get_jit_target_from_environment();
    if (target.has_gpu_feature()) {
        Var xi, yi;
        f.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
        g.compute_root().gpu_tile(x, y, xi, yi, 16, 16);
    }

    printf("Realizing function...\n");

    Buffer<int> img = g.realize({W, H}, target);

    for (int i = 0; i < W; i++) {
        for (int j = 0; j < H; j++) {
            int m = std::max(i, j);
            const int expected = std::min(std::max(m, 20), 100);
            if (img(i, j) != expected) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

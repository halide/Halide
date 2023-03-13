#include "Halide.h"

using namespace Halide;

int main() {

    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }

    Var x, y, xi, yi;
    ImageParam table(Int(32), 1);

    Func f, g, h;

    // It's possible to have a buffer simultaneously read on CPU and
    // GPU if a load from it gets lifted into a predicate used by skip
    // stages. This tests that path.

    f(x, y) = x * 2 + y + table(x);
    g(x, y) = x + y * 2 + table(y);
    h(x, y) = select(table(0) == 0, f(x, y), g(x, y));

    f.compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    g.compute_root().gpu_tile(x, y, xi, yi, 8, 8);
    h.compute_root().gpu_tile(x, y, xi, yi, 8, 8);

    Buffer<int32_t> t(32);
    t.fill(17);
    t(0) = 0;
    table.set(t);
    Buffer<int32_t> result1 = h.realize({20, 20});
    t(0) = 1;
    table.set(t);
    Buffer<int32_t> result2 = h.realize({20, 20});

    for (int y = 0; y < 20; y++) {
        for (int x = 0; x < 20; x++) {
            int c1 = x * 2 + y + (x == 0 ? 0 : 17);
            int c2 = x + y * 2 + (y == 0 ? 1 : 17);
            if (result1(x, y) != c1) {
                printf("result1(%d, %d) = %d instead of %d\n",
                       x, y, result1(x, y), c1);
                return 1;
            }
            if (result2(x, y) != c2) {
                printf("result2(%d, %d) = %d instead of %d\n",
                       x, y, result2(x, y), c2);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

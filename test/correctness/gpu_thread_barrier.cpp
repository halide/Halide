#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    if (!get_jit_target_from_environment().has_gpu_feature()) {
        printf("Not running test because no gpu target enabled\n");
        return 0;
    }

    Func f;
    Var x, y, z;

    // Construct a Func with lots of potential race conditions, and
    // then run it in thread blocks on the gpu.

    f(x, y) = x + 100 * y;

    const int passes = 10;
    for (int i = 0; i < passes; i++) {
        RDom rx(0, 10);
        // Flip each row, using spots 10-19 as temporary storage
        f(rx + 10, y) = f(9 - rx, y);
        f(rx, y) = f(rx + 10, y);
        // Flip each column the same way
        RDom ry(0, 8);
        f(x, ry + 8) = f(x, 7 - ry);
        f(x, ry) = f(x, ry + 8);
    }

    Func g;
    g(x, y) = f(0, 0)+ f(9, 7);

    g.gpu_tile(x, y, 16, 16);
    f.compute_at(g, Var::gpu_blocks());

    for (int i = 0; i < passes; i++) {
        f.update(i*4 + 0).gpu_threads(y);
        f.update(i*4 + 1).gpu_threads(y);
        f.update(i*4 + 2).gpu_threads(x);
        f.update(i*4 + 3).gpu_threads(x);
    }

    Image<int> out = g.realize(100, 100);
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int correct = 7*100 + 9;
            if (out(x, y) != correct) {
                printf("out(%d, %d) = %d instead of %d\n",
                       x, y, out(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

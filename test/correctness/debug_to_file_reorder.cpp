#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    {
        Func f, g, h, k;
        Var x, y, c;
        f(x, y, c) = cast<float>((x + y) * c);
        h(x,y,c) = f(x, y, c);

        g(x, y, c) = cast<float>((x + y) * c);
        k(x,y,c) = g(x, y, c);

        Target target = get_jit_target_from_environment();
        if (target.has_gpu_feature()) {
            f.compute_root().gpu_tile(x, y, 1, 1).reorder_storage(c, x, y).debug_to_file("reordered.tiff");
            g.compute_root().gpu_tile(x, y, 1, 1).debug_to_file("baseline.tiff");
        } else {
            f.compute_root().reorder_storage(c, x, y).debug_to_file("reordered.tiff");
            g.compute_root().debug_to_file("baseline.tiff");
        }

        int size_x = 512, size_y = 1024;
        h.realize(size_x, size_y, 3, target);
        k.realize(size_x, size_y, 3, target);
    }

    printf("Success!\n");
    return 0;

}

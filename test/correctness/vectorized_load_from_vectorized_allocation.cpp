#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // Vectorized Load from a vectorized allocation
    const int size = 80;

    Func f("f"), g("g");
    Var x("x"), y("y"), z("z");

    g(x, y, z) = x;

    f(x, y, z) = 100;
    RDom r(0, size, 0, size, 0, size);
    f(r.x, r.y, r.z) += 2 * g(r.x * r.z, r.y, r.z);

    f.update(0).vectorize(r.z, 8);

    g.compute_at(f, r.y);
    g.bound_extent(x, size * size);

    Buffer<int> im = f.realize({size, size, size});

    for (int z = 0; z < im.channels(); z++) {
        for (int y = 0; y < im.height(); y++) {
            for (int x = 0; x < im.width(); x++) {
                int correct = 100 + 2 * x * z;
                if (im(x, y, z) != correct) {
                    printf("im(%d, %d, %d) = %d instead of %d\n",
                           x, y, z, im(x, y, z), correct);
                    return 1;
                }
            }
        }
    }
    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <algorithm>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    const int size = 32;

    Buffer<float> noise(size, size);
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            noise(j, i) = (float)rand() / (float)RAND_MAX;
        }
    }

    // Define a seam carving-esque energy.  The meaning of this
    // depends on the interleaving of the x and y dimensions during
    // the reduction update. This is why we have RDoms instead of just
    // using multiple RVars in an update.

    Func energy;
    RDom r(0, noise.width(), 1, noise.height() - 1);
    Expr xm = max(r.x - 1, 0), xp = min(r.x + 1, noise.width() - 1);
    energy(x, y) = 0.0f;
    energy(x, 0) = noise(x, 0);  // The first row is just the first row of the input.
    energy(r.x, r.y) = noise(r.x, r.y) + min(energy(xm, r.y - 1),
                                             energy(r.x, r.y - 1),
                                             energy(xp, r.y - 1));

    Buffer<float> im_energy = energy.realize({size, size});
    Buffer<float> ref_energy(size, size);
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int xm = std::max(x - 1, 0);
            int xp = std::min(x + 1, size - 1);

            float incr = 0.0f;
            if (y > 0) {
                incr = std::min(ref_energy(xm, y - 1), std::min(ref_energy(x, y - 1), ref_energy(xp, y - 1)));
            }
            ref_energy(x, y) = noise(x, y) + incr;

            float delta = ref_energy(x, y) - im_energy(x, y);
            if (fabs(delta) > 1e-5) {
                printf("energy(%d,%d) was %f instead of %f\n", x, y, im_energy(x, y), ref_energy(x, y));
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

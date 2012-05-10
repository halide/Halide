#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Image<double> noise(32,32);
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            noise(j,i) = (double)rand() / RAND_MAX;
        }
    }

    // Define a seam carving-esque energy
    // The meaning of this depends on the interleaving of the x and y 
    // dimensions during the reduction update
    Func energy;
    RDom ry(1, noise.height());
    energy(x, y)  = noise(clamp(x, 0, noise.width()), clamp(y, 0, noise.height()));
    energy(x, ry) = noise(clamp(x, 0, noise.width()), clamp(ry, 0, noise.height()))
                    + min( min( energy(x-1, ry-1), energy(x, ry-1) ), energy(x+1, ry-1));

    Image<double> im_energy = energy.realize(32,32);
    Image<double> ref_energy(noise);
    for (int y = 1; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            double incr = std::min(ref_energy(x-1, y-1), std::min(ref_energy(x, y-1), ref_energy(x+1, y-1)));
            ref_energy(x, y) += incr;
            if (ref_energy(x,y) != im_energy(x,y)) {
                printf("energy(%d,%d) was %f instead of %f\n", x, y, im_energy(x,y), ref_energy(x,y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

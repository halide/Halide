#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    const int size = 32;

    Image<double> noise(size, size);
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            noise(j,i) = (double)rand() / RAND_MAX;
        }
    }

    // Define a seam carving-esque energy
    // The meaning of this depends on the interleaving of the x and y 
    // dimensions during the reduction update
    Func clamped;
    clamped(x, y) = noise(clamp(x, 0, size-1), clamp(y, 0, size-1));
    
    Func energy;
    RDom ry(1, noise.height()-1);
    energy(x, y)  = clamped(x, y);
    Expr xm = clamp(x-1, 0, size-1);
    Expr xp = clamp(x+1, 0, size-1);
    energy(x, ry) = clamped(x, ry) + min(min(energy(xm, ry-1), energy(x, ry-1)), energy(xp, ry-1));

    Image<double> im_energy = energy.realize(size,size);
    Image<double> ref_energy(noise);
    for (int y = 1; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int xm = std::max(x-1, 0);
            int xp = std::min(x+1, size-1);
            double incr = std::min(ref_energy(xm, y-1), std::min(ref_energy(x, y-1), ref_energy(xp, y-1)));
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

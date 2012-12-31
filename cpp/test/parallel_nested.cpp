#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y, z;
    Func f;

    Param<int> k;
    k.set(3);

    f(x, y, z) = x*y+z*k+1;

    f.parallel(x);
    f.parallel(y);
    f.parallel(z);

    Image<int> im = f.realize(8, 8, 8);

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            for (int z = 0; z < 8; z++) {
                if (im(x, y, z) != x*y+z*3+1) {
                    printf("im(%d, %d, %d) = %d\n", x, y, z, im(x, y, z));
                    return -1;
                }
            }
        }
    }
    
    printf("Success!\n");
    return 0;
}

#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x, y, z;
    Func f;

    Uniform<int> k = 3;

    f(x, y, z) = x*y+z*k;

    f.parallel(x);
    f.parallel(y);
    f.parallel(z);

    Image<int> im = f.realize(16, 16, 16);

    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 16; y++) {
            for (int z = 0; z < 16; z++) {
                if (im(x, y, z) != x*y+z*3) {
                    printf("im(%d, %d) = %d\n", x, y, z, im(x, y, z));
                    return -1;
                }
            }
        }
    }
    
    printf("Success!");
    return 0;
}

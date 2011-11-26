#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g, h;

    printf("Defining function...\n");

    f(x, y) = Max(x, y);
    g(x, y) = Min(x, y);
    h(x, y) = Clamp(x+y, 20, 100);

    printf("Realizing function...\n");

    Image<int> imf = f.realize(32, 32);
    Image<int> img = g.realize(32, 32);
    Image<int> imh = h.realize(32, 32);

    for (size_t i = 0; i < 32; i++) {
        for (size_t j = 0; j < 32; j++) {
            if (imf(i, j) != (i > j ? i : j)) {
                printf("imf[%d, %d] = %d\n", i, j, imf(i, j));
                return -1;
            }
            if (img(i, j) != (i < j ? i : j)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return -1;
            }
            if (imh(i, j) != (i+j < 20 ? 20 :
                              i+j > 100 ? 100 :
                              i+j)) {
                printf("imh[%d, %d] = %d\n", i, j, imh(i, j));
                return -1;
            }
            
        }
    }

    printf("Success!\n");
    return 0;
}

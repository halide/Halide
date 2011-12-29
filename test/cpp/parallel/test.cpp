#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x;
    Func f;

    Uniform<int> k = 3;

    f(x) = x*k;

    f.parallel(x);

    Image<int> im = f.realize(16);

    for (int i = 0; i < 16; i++) {
        if (im(i) != i*3) {
            printf("im(%d) = %d\n", i, im(i));
            return -1;
        }
    }
    
    printf("Success!");
    return 0;
}

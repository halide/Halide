#include "FImage.h"
#include <math.h>

using namespace FImage;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    printf("Defining function...\n");

    f(x) = sqrt(Cast<float>(x));
    
    f.vectorize(x, 4);
    Image<float> im = f.realize(32);

    for (int i = 0; i < 32; i++) {
        float correct = sqrtf(i);
        if (fabs(im(i) - correct) > 0.001) {
            printf("im(%d) = %f instead of %f\n", i, im(i), correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}

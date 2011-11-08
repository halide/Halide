#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x, y;
    Func f, g;

    printf("Defining function...\n");

    f(x, y) = 2.0f;

    // implicit for all y
    g(x) = f(x) + f(x-1);

    // implicit for all x, y on both sides
    Func h;
    h = g + f;

    printf("Realizing function...\n");

    Image<float> im = h.realize(4, 4);

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            if (im(x, y) != 6.0f) {
                printf("im(%d, %d) = %f\n", x, y, im(x, y));
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}

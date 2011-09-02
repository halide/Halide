#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x(0, 100), y(0, 100);
    Image im(100, 100);       

    im(x, y) = x*y;

    x.vectorize(4);

    im.evaluate();

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            printf("%d ", im(x, y));
        }
        printf("\n");
    }

    return 0;
}

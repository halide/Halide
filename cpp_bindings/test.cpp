#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x(0, 100);
    Image im(100);       

    im(x) = x;

    x.vectorize(4);

    im.evaluate();

    Image im2(100);

    im2(x) = im(x + 1);

    im2.evaluate();

    for (int x = 0; x < 16; x++) {
        printf("%d ", im2(x));
    }

    return 0;
}

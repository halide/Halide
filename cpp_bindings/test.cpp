#include "FImage.h"

using namespace FImage;

int main(int argc, char **argv) {
    Var x(0, 128);
    Image im(128);       

    for (int i = 0; i < 128; i++) 
        im(i) = i;

    x.vectorize(4);
    x.unroll(4);

    Image im2(128);

    im2(x) = im(x) + im(x+1);

    im2.evaluate();

    for (int x = 0; x < 16; x++) {
        printf("%d ", im2(x));
    }

    return 0;
}

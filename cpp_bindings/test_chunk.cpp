#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

int main(int argc, char **argv) {
    Var x;
    Func f, g;

    printf("Defining function...\n");

    f(x) = 2.0f;
    g(x) = f(x+1) + f(x-1);

    Var xo, xi;

    g.split(x, xo, xi, 4);
    //f.chunk(xi);
    f.chunk(xi, Range(xo*4, 4));
    //f.vectorize(x);
    //g.vectorize(xi);

    printf("Realizing function...\n");

    Image im = g.realize(1024);

    return 0;
}

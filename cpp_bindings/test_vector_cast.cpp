#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3072
#define H 3072

float operator-(const timeval &after, const timeval &before) {
    int ds = after.tv_sec - before.tv_sec;
    int du = after.tv_usec - before.tv_usec;
    return (ds * 1000.0f) + (du / 1000.0f);
}

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f");
    Image<unsigned char> im(W, H);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = rand() & 255;
        }
    }
    
    Var xi("xi");
    f(x, y)  = Cast(UInt(16), im(x, y));
    f.split(x, x, xi, 8);
    f.vectorize(xi);

    Image<unsigned short> out = f.realize(W, H);
    
    Func g("g");
    g(x, y) = Cast(UInt(8), out(x, y));
    g.split(x, x, xi, 8);
    g.vectorize(xi);

    Image<unsigned char> out2 = g.realize(W, H);

    timeval before, after;
    gettimeofday(&before, NULL);
    f.realize(out);
    g.realize(out2);
    gettimeofday(&after, NULL);
    printf("jitted code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%d ", im(x, 10));
    }
    printf("\n");
    for (int x = 0; x < 16; x++) {
        printf("%d ", out(x, 10));
    }
    printf("\n");
    for (int x = 0; x < 16; x++) {
        printf("%d ", out2(x, 10));
    }
    printf("\n");

    return 0;
}

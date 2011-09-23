#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3008
#define H 3008

float operator-(const timeval &after, const timeval &before) {
    int ds = after.tv_sec - before.tv_sec;
    int du = after.tv_usec - before.tv_usec;
    return (ds * 1000.0f) + (du / 1000.0f);
}

timeval now() {
    timeval t;
    gettimeofday(&t, NULL);
    return t;
}

int main(int argc, char **argv) {
    Var x(64, W-64), y(0, H);
    Image im(W, H);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = y*H+x;
        }
    }

    //x.unroll(2);
    //x.vectorize(4);
    //y.unroll(4);

    Func im2("im2", x, y, im(x, y)*3.0f);

    Image im3(W, H);
    im3(x, y) = im2(x, y);

    im3.evaluate();

    return 0;
}

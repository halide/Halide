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

int main(int argc, char **argv) {
    Var x(64, W-64), y(0, H);
    Image im(W, H);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = y*H+x;
        }
    }

    //x.unroll(4);
    x.vectorize(4);
    //y.unroll(4);

    Image im2(W, H);

    im2(x, y) = im(x, y) + im(x+1, y);

    im2.evaluate();

    timeval before, after;
    gettimeofday(&before, NULL);
    im2.evaluate();
    gettimeofday(&after, NULL);
    printf("jitted code: %f ms\n", after - before);
    
    gettimeofday(&before, NULL);
    for (int y = 0; y < H; y++) {
        for (int x = 64; x < W-64; x++) {
            im2(x, y) = im(x, y) + im(x+1, y) + im(x, y);
        }
    }
    gettimeofday(&after, NULL);
    printf("compiled code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%d ", im2(x));
    }

    return 0;
}

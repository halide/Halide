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
    Var x, y;

    Image im(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = x+y;
        }
    }

    Func f1;
    f1(x, y) = im(x, y)+im(x-1, y);

    Func f2;
    f2(x, y) = f1(x, y)+f1(x, y-1);

    // Evaluate all of f into a buffer
    Image im3 = f2.realize(W, H);

    printf("im3:\n");
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            printf("%5.0f ", im3(x, y));
        }
        printf("\n");
    }

    return 0;
}

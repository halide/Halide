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
    Var x(0, W), y(0, H);
    Image im(W, H);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = x+y;
        }
    }

    //x.unroll(2);
    //x.vectorize(4);
    //y.unroll(4);

    Func im2("func", x, y, im(x, y)*3.0f);    

    Image im3(W, H);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im3(x, y) = -1;
        }
    }

    im3(x, y) = im2(x, y)+1.0f;

    im3.evaluate();

    printf("im3:\n");
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            printf("%5.0f ", im3(x, y));
        }
        printf("\n");
    }

    return 0;
}

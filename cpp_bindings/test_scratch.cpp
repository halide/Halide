#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3072
#define H 3072

template<typename T>
inline T do_math(const T &a, const T &b) {
    T acc = a - b;
    for (int i = 0; i < 0; i++) {
        acc += a - b;
    }
    return acc;
}



float operator-(const timeval &after, const timeval &before) {
    int ds = after.tv_sec - before.tv_sec;
    int du = after.tv_usec - before.tv_usec;
    return (ds * 1000.0f) + (du / 1000.0f);
}

int main(int argc, char **argv) {
    Var x("x"), y("y");
    Func f("f"), g("g"), h("h"), i("i");
    Image<unsigned char> im(W+32, H+32);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = (x > 24) ? 40 : 20;
        }
    }

    printf("Defining function...\n");
    
    Func in("in"), blurx("blurx"), blury("blury"), high("high"), out("out");
    in(x, y)    = Cast(UInt(16), im(x+16, y+16));
    blurx(x, y) = (in(x-1, y) + in(x, y)*2 + in(x+1, y))/4;
    blury(x, y) = (blurx(x, y-1) + blurx(x, y)*2 + blurx(x, y+1))/4;
    high(x, y)  = in(x, y) - blury(x, y);
    out(x, y)   = Cast(UInt(8), in(x, y) + high(x, y));

    //f.trace();
    //g.trace();
    //h.trace(); 

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi");

    if (argc > 1) {
        //int chunk = atoi(argv[1]);
        // Compute out scanline-at-a-time in vectors
        out.split(x, xo, xi, 8);
        out.vectorize(xi);

        // Compute high scanline-at-a-time
        //high.chunk(xo, Range(0, W) * Range(y, 1));
        //high.split(x, xo, xi, 8);
        //high.vectorize(xi);

        //blury.chunk(xo, Range(0, W) * Range(y, 1));
        //blury.split(x, xo, xi, 8);
        //blury.vectorize(xi);

        // blury needs three scanlines of blurx
        //blurx.chunk(xo, Range(0, W) * Range(y-1, 3));
        //blurx.split(x, xo, xi, 8);
        //blurx.vectorize(xi);

        //in.chunk(xo, Range(-8, W+8) * Range(y-1, 3));
        //in.split(x, xo, xi, 8);
        //in.vectorize(xi);
    }


    printf("Realizing function...\n");

    Image<unsigned char> im_out = out.realize(W, H);

    timeval before, after;
    gettimeofday(&before, NULL);
    out.realize(im_out);
    gettimeofday(&after, NULL);
    printf("jitted code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%d ", im(x+16, 26));
    }
    printf("\n");
    for (int x = 0; x < 16; x++) {
        printf("%d ", im_out(x, 10));
    }
    printf("\n");

    return 0;
}

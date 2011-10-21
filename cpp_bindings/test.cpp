#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3072
#define H 3072

template<typename T>
inline T do_math(const T &a, const T &b) {
    T acc = 1.0f;
    for (int i = 0; i < 1; i++) {
        acc *= a - b;
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
    Func f("f"), g("g"), h("h");
    Image im(W+16, H+16);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = ((double)rand())/RAND_MAX;
        }
    }

    printf("Defining function...\n");
    


    //f(x, y) = Debug(do_math<Expr>(im(x, y), im(x+3, y)), "Evaluating f at: ", x, y);
    //g(x, y) = Debug(do_math<Expr>(f(x, y), f(x, y+3)), "Evaluating g at: ", x, y);

    f(x, y) = do_math<Expr>(im(x, y), im(x+3, y));
    g(x, y) = do_math<Expr>(f(x, y), f(x, y+3));
    h(x, y) = do_math<Expr>(g(x+3, y), g(x, y));

    //f.trace();
    //g.trace();
    //h.trace(); 

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi");

    if (argc > 1) {
        int chunk = atoi(argv[1]);
        h.split(y, yo, yi, chunk);
        g.chunk(x, Range(0, W+4) * Range(yo*chunk + yi, 1));
        f.chunk(yi, Range(0, W+4) * Range(yo*chunk, chunk+3)); 

        /*
        h.split(x, xo, xi, 4);
        h.vectorize(xi);

        g.split(x, xo, xi, 4);
        g.vectorize(xi);

        f.split(x, xo, xi, 4);
        f.vectorize(xi);
        */
    }


    printf("Realizing function...\n");

    Image im2 = h.realize(W, H);

    timeval before, after;
    gettimeofday(&before, NULL);
    h.realize(im2);
    gettimeofday(&after, NULL);
    printf("jitted code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%3.1f ", im2(x, 10));
    }
    printf("\n");

    // Clear it before the next run
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im2(x, y) = -1; 
        }
    }    
    
    Image tmp1(W+16, H+16);
    Image tmp2(W+16, H+16);
    

    gettimeofday(&before, NULL);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp1(x, y) = do_math(im(x, y), im(x+3, y));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp2(x, y) = do_math(tmp1(x, y), tmp1(x, y+3));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im2(x, y) = do_math(tmp2(x+3, y), tmp2(x, y));
        }
    }

    gettimeofday(&after, NULL);
    printf("compiled code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%3.1f ", im2(x, 10));
    }
    printf("\n");

    return 0;
}

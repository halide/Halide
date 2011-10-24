#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3072
#define H 3072

template<typename T>
inline T do_math(const T &a, const T &b) {
    T acc = 1.0f;
    for (int i = 0; i < 2; i++) {
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
    Func f("f"), g("g"), h("h"), i("i");
    Image im(W+16, H+16);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = ((double)rand())/RAND_MAX;
        }
    }

    printf("Defining function...\n");
    
    f(x, y) = do_math<Expr>(im(x, y), im(x+4, y));
    g(x, y) = do_math<Expr>(f(x, y), f(x, y+4));
    h(x, y) = do_math<Expr>(f(x+4, y), f(x, y));
    i(x, y) = do_math<Expr>(g(x, y), h(x, y));

    //f.trace();
    //g.trace();
    //h.trace(); 

    Var xo("xo"), xi("xi"), yo("yo"), yi("yi");

    if (argc > 1) {
        int chunk = atoi(argv[1]);
        i.split(y, yo, yi, chunk);
        f.chunk(yi, Range(0, W+4) * Range(yo*chunk, chunk+3)); 
        
        f.split(x, xo, xi, 4);
        f.vectorize(xi);

        i.split(x, xo, xi, 4);
        i.vectorize(xi);
        g.chunk(xi, Range(xo*4, 4) * Range(yo * chunk + yi, 1));
        g.vectorize(x);
        h.chunk(xi, Range(xo*4, 4) * Range(yo * chunk + yi, 1));
        h.vectorize(x);

    }


    printf("Realizing function...\n");

    Image im2 = i.realize(W, H);

    timeval before, after;
    gettimeofday(&before, NULL);
    i.realize(im2);
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
    
    Image tmp_f(W+16, H+16);
    Image tmp_g(W+16, H+16);
    Image tmp_h(W+16, H+16);
    Image tmp_i(W+16, H+16);

    gettimeofday(&before, NULL);

    f(x, y) = do_math<Expr>(im(x, y), im(x+3, y));
    g(x, y) = do_math<Expr>(f(x, y), f(x, y+3));
    h(x, y) = do_math<Expr>(f(x+3, y), f(x, y));
    i(x, y) = do_math<Expr>(g(x, y), h(x, y));

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp_f(x, y) = do_math(im(x, y), im(x+4, y));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp_g(x, y) = do_math(tmp_f(x, y), tmp_f(x, y+4));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp_h(x, y) = do_math(tmp_f(x+4, y), tmp_f(x, y));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp_i(x, y) = do_math(tmp_g(x, y), tmp_h(x, y));
        }
    }

    gettimeofday(&after, NULL);
    printf("compiled code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%3.1f ", tmp_i(x, 10));
    }
    printf("\n");

    return 0;
}

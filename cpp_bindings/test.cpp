#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3072
#define H 3072

template<typename T, int N>
struct _sum {
    static inline T go(const T &a) {return _sum<T, N-1>::go(a) + a;}
};

template<typename T>
struct _sum<T, 1> {
    static inline T go(const T &a) {return a;}
};

template<typename T, int N> 
struct _do_math {
    static inline T go(const T &a, const T &b) {return _do_math<T, N-1>::go(a, b) * (a + _sum<T, N>::go(b));}
};

template<typename T>
struct _do_math<T, 0> {
    static inline T go(const T &a, const T &b) {return a;}
};

template<typename T>
inline T do_math(const T &a, const T &b) {
    return _do_math<T,0>::go(a, b);
}


float operator-(const timeval &after, const timeval &before) {
    int ds = after.tv_sec - before.tv_sec;
    int du = after.tv_usec - before.tv_usec;
    return (ds * 1000.0f) + (du / 1000.0f);
}

int main(int argc, char **argv) {
    Var x, y;
    Func f, g, h;
    Image im(W+16, H+16);       

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im(x, y) = 1;
        }
    }

    printf("Defining function...\n");
    


    //f(x, y) = Debug(do_math<Expr>(im(x, y), im(x+3, y)), "Evaluating f at: ", x, y);
    //g(x, y) = Debug(do_math<Expr>(f(x, y), f(x, y+3)), "Evaluating g at: ", x, y);

    f(x, y) = do_math<Expr>(im(x, y), im(x+3, y));
    g(x, y) = do_math<Expr>(f(x, y), f(x, y+3));
    h(x, y) = do_math<Expr>(g(x+3, y), g(x, y));

    Var xo, xi, yo, yi;

    if (argc > 1) {
        int chunk = atoi(argv[1]);
        h.split(y, yo, yi, chunk);
        g.chunk(yi, Range(0, W+4) * Range(yo*chunk, chunk));
        f.chunk(y, Range(0, W+4) * Range(yo*chunk, chunk+3)); 

        h.split(x, xo, xi, 4);
        h.vectorize(xi);

        g.split(x, xo, xi, 4);
        g.vectorize(xi);

        f.split(x, xo, xi, 4);
        f.vectorize(xi);
    }


    printf("Realizing function...\n");

    Image im2 = h.realize(W, H);

    timeval before, after;
    gettimeofday(&before, NULL);
    g.realize(im2);
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
    
    Image tmp(W+16, H+16);
    

    gettimeofday(&before, NULL);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            tmp(x, y) = do_math(im(x, y), im(x+3, y));
        }
    }

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            im2(x, y) = do_math(tmp(x, y), tmp(x, y+3));
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

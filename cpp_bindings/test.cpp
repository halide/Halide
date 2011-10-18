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
            im(x, y) = 0.11;
        }
    }

    printf("Defining function...\n");
    


    f(x, y) = do_math<Expr>(im(x, y), im(x+3, y));
    h(x, y) = Debug(f(x, y), "Evaluating f at: ", x, y, f(x, y));
    g(x, y) = do_math<Expr>(h(x, y), h(x, y+3));

    //f(x, y) = im(x, y) + im(x+1, y);
    //g(x, y) = f(x, y) + f(x, y+1);

    Var xo, xi, yo, yi;

    if (argc > 1) {
        int chunk = atoi(argv[1]);
        g.split(y, yo, yi, chunk);
        h.chunk(yi, Range(0, W) * Range(yo*chunk, chunk+4));

        //g.split(x, xo, xi, 4);
        //g.vectorize(xi);

        h.split(x, xo, xi, 4);
        h.vectorize(xi);
    }


    printf("Realizing function...\n");

    Image im2 = g.realize(W, H);

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

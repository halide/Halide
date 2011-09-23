#include "FImage.h"
#include <sys/time.h>

using namespace FImage;

#define W 3008
#define H 3008

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
T do_math(const T &a, const T &b) {
    return _do_math<T,10>::go(a, b);
}


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

    //x.unroll(2);
    //x.vectorize(4);
    //y.unroll(4);

    Image im2(W, H);

    im2(x, y) = do_math<Expr>(im(x, y), im(x+1, y));

    im2.evaluate();

    timeval before, after;
    gettimeofday(&before, NULL);
    im2.evaluate();
    gettimeofday(&after, NULL);
    printf("jitted code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%d ", im2(x));
    }
    printf("\n");
    
    gettimeofday(&before, NULL);
    for (int y = 0; y < H; y++) {
        for (int x = 64; x < W-64; x++) {
            im2(x, y) = do_math<float>(im(x, y), im(x+1, y));
        }
    }
    gettimeofday(&after, NULL);
    printf("compiled code: %f ms\n", after - before);

    for (int x = 0; x < 16; x++) {
        printf("%d ", im2(x));
    }
    printf("\n");

    return 0;
}

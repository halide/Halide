#include <stdio.h>
#include <Halide.h>

using namespace Halide;

int count = 0;
extern "C" int call_counter(int x) {
    count++;
    return 0;
}
HalideExtern_1(int, call_counter, int);

int main(int argc, char **argv) {
    Func f, g;
    Var x;

    f(x) = call_counter(x);
    g(x) = f(x) + f(x-1);

    f.store_root().compute_at(g, x);

    Image<int> im = g.realize(100);

    // f should be able to tell that it only needs to compute each value once
    if (count != 101) {
        printf("f was called %d times instead of %d times\n", count, 101);
        //return -1;
    }

    // Now try with a reduction
    count = 0;
    RDom r(0, 100);
    Var y;
    g = Func();
    f = Func();

    f(x, y) = 0;
    f(r, y) = call_counter(r);
    f.store_root().compute_at(g, y);

    g(x, y) = f(x, y) + f(x, y-1);
    
    Image<int> im2 = g.realize(10, 10);

    // For each value of y, f should be evaluated over (0 .. 100) in
    // x, and (y .. y-1) in y. Sliding window optimization means that
    // we can skip the y-1 case in all but the first iteration.
    if (count != 100 * 11) {
        printf("f was called %d times instead of %d times\n", count, 100*11);
        return -1;
    }

    // Now try sliding over multiple dimensions at once
    f = Func();
    g = Func();
    
    count = 0;
    f(x, y) = call_counter(x);
    g(x, y) = f(x-1, y) + f(x, y) + f(x, y-1);
    f.store_root().compute_at(g, x);

    Image<int> im3 = g.realize(10, 10);
    
    if (count != 11*11) {
        printf("f was called %d times instead of %d times\n", count, 11*11);
        return -1;
    }

    f = Func();
    g = Func();

    // Now a trickier example. In order for this to work, Halide would have to slide diagonally. We don't handle this.
    count = 0;
    f(x, y) = call_counter(x);    
    // When x was two smaller the second term was computed. When y was two smaller the third term was computed.
    g(x, y) = f(x+y, x-y) + f((x-2)+y, (x-2)-y) + f(x+(y-2), x-(y-2)); 
    f.store_root().compute_at(g, x);

    Image<int> im4 = g.realize(10, 10);
    printf("%d\n", count);
    if (count != 1500) {
        printf("f was called %d times instead of %d times\n", count, 1500);
        return -1;
    }

    printf("Success!\n");
    return 0;    
}

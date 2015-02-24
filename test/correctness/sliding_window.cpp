#include <stdio.h>
#include "Halide.h"

using namespace Halide;

#ifdef _MSC_VER
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int count = 0;
extern "C" DLLEXPORT int call_counter(int x, int y) {
    count++;
    return 0;
}
HalideExtern_2(int, call_counter, int, int);

extern "C" void *my_malloc(void *, size_t x) {
    printf("Malloc wasn't supposed to be called!\n");
    exit(-1);
}

extern "C" void my_free(void *, void *) {
}

int main(int argc, char **argv) {
    Func f1("f1"), g1("g1");
    Var x("x");

    f1(x) = call_counter(x, 0);
    g1(x) = f1(x) + f1(x-1);

    f1.store_root().compute_at(g1, x);

    Image<int> im1 = g1.realize(100);

    // f should be able to tell that it only needs to compute each value once
    if (count != 101) {
        printf("f was called %d times instead of %d times\n", count, 101);
        return -1;
    }

    // Now try with a reduction
    count = 0;
    RDom r(0, 100);
    Var y("y");
    Func f2("f2"), g2("g2");

    f2(x, y) = 0;
    f2(r, y) = call_counter(r, y);
    f2.store_root().compute_at(g2, y);

    g2(x, y) = f2(x, y) + f2(x, y-1);

    Image<int> im2 = g2.realize(10, 10);

    // For each value of y, f should be evaluated over (0 .. 100) in
    // x, and (y .. y-1) in y. Sliding window optimization means that
    // we can skip the y-1 case in all but the first iteration.
    if (count != 100 * 11) {
        printf("f was called %d times instead of %d times\n", count, 100*11);
        return -1;
    }

    // Now try sliding over multiple dimensions at once
    Func f3("f3"), g3("g3");

    count = 0;
    f3(x, y) = call_counter(x, y);
    g3(x, y) = f3(x-1, y) + f3(x, y) + f3(x, y-1);
    f3.store_root().compute_at(g3, x);

    Image<int> im3 = g3.realize(10, 10);

    if (count != 11*11) {
        printf("f was called %d times instead of %d times\n", count, 11*11);
        return -1;
    }

    Func f4("f4"), g4("g4");

    // Now a trickier example. In order for this to work, Halide would have to slide diagonally. We don't handle this.
    count = 0;
    f4(x, y) = call_counter(x, y);
    // When x was two smaller the second term was computed. When y was two smaller the third term was computed.
    g4(x, y) = f4(x+y, x-y) + f4((x-2)+y, (x-2)-y) + f4(x+(y-2), x-(y-2));
    f4.store_root().compute_at(g4, x);

    Image<int> im4 = g4.realize(10, 10);
    if (count != 1500) {
        printf("f was called %d times instead of %d times\n", count, 1500);
        return -1;
    }

    // Now make sure Halide folds the example in Func.h down to a stack allocation
    Func f5("f5"), g5("g5");
    f5(x, y) = x*y;
    g5(x, y) = f5(x, y) + f5(x+1, y) + f5(x, y+1) + f5(x+1, y+1);
    f5.store_at(g5, y).compute_at(g5, x);
    g5.set_custom_allocator(&my_malloc, &my_free);
    Image<int> im5 = g5.realize(10, 10);

    printf("Success!\n");
    return 0;
}

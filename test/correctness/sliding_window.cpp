#include <stdio.h>
#include "Halide.h"

using namespace Halide;

#ifdef _WIN32
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
    Var x, y;

    {
        Func f, g;

        f(x) = call_counter(x, 0);
        g(x) = f(x) + f(x-1);

        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize(100);

        // f should be able to tell that it only needs to compute each value once
        if (count != 101) {
            printf("f was called %d times instead of %d times\n", count, 101);
            return -1;
        }
    }

    // Try again where there's a containing stage
    {
        count = 0;
        Func f, g, h;
        f(x) = call_counter(x, 0);
        g(x) = f(x) + f(x-1);
        h(x) = g(x);

        f.store_root().compute_at(g, x);
        g.compute_at(h, x);

        Buffer<int> im = h.realize(100);
        if (count != 101) {
            printf("f was called %d times instead of %d times\n", count, 101);
            return -1;
        }
    }

    // Add an inner vectorized dimension.
    {
        count = 0;
        Func f, g, h;
        Var c;
        f(x, c) = call_counter(x, c);
        g(x, c) = f(x + 1, c) - f(x, c);
        h(x, c) = g(x, c);

        f.store_root()
            .compute_at(h, x)
            .reorder(c, x)
            .reorder_storage(c, x)
            .bound(c, 0, 4)
            .vectorize(c);

        g.compute_at(h, x);

        h.reorder(c, x).reorder_storage(c, x).bound(c, 0, 4).vectorize(c);

        Buffer<int> im = h.realize(100, 4);
        if (count != 404) {
            printf("f was called %d times instead of %d times\n", count, 404);
            return -1;
        }
    }

    // Now try with a reduction
    {
        count = 0;
        RDom r(0, 100);
        Func f, g;

        f(x, y) = 0;
        f(r, y) = call_counter(r, y);
        f.store_root().compute_at(g, y);

        g(x, y) = f(x, y) + f(x, y-1);

        Buffer<int> im = g.realize(10, 10);

        // For each value of y, f should be evaluated over (0 .. 100) in
        // x, and (y .. y-1) in y. Sliding window optimization means that
        // we can skip the y-1 case in all but the first iteration.
        if (count != 100 * 11) {
            printf("f was called %d times instead of %d times\n", count, 100*11);
            return -1;
        }
    }

    {
        // Now try sliding over multiple dimensions at once
        Func f, g;

        count = 0;
        f(x, y) = call_counter(x, y);
        g(x, y) = f(x-1, y) + f(x, y) + f(x, y-1);
        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize(10, 10);

        if (count != 11*11) {
            printf("f was called %d times instead of %d times\n", count, 11*11);
            return -1;
        }
    }

    {
        Func f, g;

        // Now a trickier example. In order for this to work, Halide would have to slide diagonally. We don't handle this.
        count = 0;
        f(x, y) = call_counter(x, y);
        // When x was two smaller the second term was computed. When y was two smaller the third term was computed.
        g(x, y) = f(x+y, x-y) + f((x-2)+y, (x-2)-y) + f(x+(y-2), x-(y-2));
        f.store_root().compute_at(g, x);

        Buffer<int> im = g.realize(10, 10);
        if (count != 1500) {
            printf("f was called %d times instead of %d times\n", count, 1500);
            return -1;
        }
    }

    {
        // Now make sure Halide folds the example in Func.h down to a stack allocation
        Func f, g;
        f(x, y) = x*y;
        g(x, y) = f(x, y) + f(x+1, y) + f(x, y+1) + f(x+1, y+1);
        f.store_at(g, y).compute_at(g, x);
        g.set_custom_allocator(&my_malloc, &my_free);
        Buffer<int> im = g.realize(10, 10);
    }

    printf("Success!\n");
    return 0;
}

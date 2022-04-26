#include "Halide.h"
#include <stdio.h>

using namespace Halide;

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

int counter = 0;
extern "C" DLLEXPORT int call_count(int x) {
    counter++;
    assert(counter > 0);
    return 99;
}
HalideExtern_1(int, call_count, int);

void check(Buffer<int> im) {
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 99 * 3;
            if (im(x, y) != correct) {
                printf("Value at %d %d was %d instead of %d\n",
                       x, y, im(x, y), correct);
                exit(-1);
            }
        }
    }
}

int main(int argc, char **argv) {

    Var x, y;

    {
        // Could slide this reduction over y, but we don't, because it's
        // too hard to implement bounds analysis on the intermediate
        // stages.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(0, y);
        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize({2, 10}));

        int correct = 24;
        if (counter != correct) {
            printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
            return -1;
        }
    }

    {
        // Can't slide this reduction over y, because the second stage scatters.
        Func f("f");
        f(x, y) = x;
        f(x, x) += f(x, 0) + f(x, 1);
        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize({2, 10}));

        int correct = 60;
        if (counter != correct) {
            printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
            return -1;
        }
    }

    {
        // Would be able to slide this so that we only have to compute
        // one new row of f per row of g, but the unroll in the first
        // stage forces evaluations of size two in y, which would
        // clobber earlier values of the final stage of f, so we have
        // to compute the final stage of f two rows at a time as well.

        // The result is that we extend the loop to warm up f by 2
        // iterations. This adds up to 2*(12*2) = 48 evaluations of f.
        Func f("f");
        f(x, y) = x;
        f(0, y) += f(1, y) + f(2, y);
        f(x, y) = call_count(f(x, y));

        f.unroll(y, 2);
        f.update(0).unscheduled();
        f.update(1).unscheduled();

        Func g("g");
        g(x, y) = f(x, y) + f(x, y - 1) + f(x, y - 2);

        f.store_root().compute_at(g, y);

        counter = 0;
        check(g.realize({2, 10}));

        int correct = 48;
        if (counter != correct) {
            printf("Failed sliding a reduction: %d evaluations instead of %d\n", counter, correct);
            return -1;
        }
    }

    printf("Success!\n");
    return 0;
}

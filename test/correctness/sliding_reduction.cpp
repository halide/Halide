#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int counter = 0;
extern "C" int call_count(int x) {
    counter++;
    assert(counter > 0);
    return x;
}
HalideExtern_1(int, call_count, int);

int main(int argc, char **argv) {

    {
        Func f("f");
        Var x, y;
        f(x, y) = x;

        // This update step reads a larger area of the initialization,
        // which breaks sliding forwards or backwards
        f(0, y) += f(1, y-1) + f(1, y+1);

        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);

        f.store_root().compute_at(g, y);

        counter = 0;
        g.realize(2, 10);

        if (counter != 60) {
            printf("Failed sliding forwards on a reduction update that reads a "
                   "larger area of the pure definition: %d\n", counter);
            return -1;
        }
    }

    {
        Func f("f");
        Var x, y;
        f(x, y) = x;

        // This update step looks forwards along y, so it can slide
        // forwards but not backwards.
        f(0, y) += f(1, y) + f(1, y+1);

        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);

        f.store_root().compute_at(g, y);

        counter = 0;
        g.realize(2, 10);

        if (counter != 24) {
            printf("Failed sliding forwards on a forwards looking reduction update: %d\n", counter);
            return -1;
        }
    }

    {
        Func f("f");
        Var x, y;
        f(x, y) = x;

        // Same as before but sliding backwards. Shouldn't work.
        f(0, y) += f(1, y) + f(1, y+1);

        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, 100-y) + f(x, 100-(y-1)) + f(x, 100-(y-2));

        f.store_root().compute_at(g, y);

        counter = 0;
        g.realize(2, 10);

        if (counter != 60) {
            printf("Failed sliding backwards on a forwards looking reduction update: %d\n", counter);
            return -1;
        }
    }

    {
        Func f("f");
        Var x, y;
        f(x, y) = x;

        // This update step looks backwards along y, so it can slide
        // backwards but not forwards.
        f(0, y) += f(1, y) + f(1, y-1);

        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, 100-y) + f(x, 100-(y-1)) + f(x, 100-(y-2));

        f.store_root().compute_at(g, y);

        counter = 0;
        g.realize(2, 10);

        if (counter != 24) {
            printf("Failed sliding backwards on a backwards looking reduction update: %d\n", counter);
            return -1;
        }
    }

    {
        Func f("f");
        Var x, y;
        f(x, y) = x;

        // Same as before but sliding forwards. Shouldn't work.
        f(0, y) += f(1, y) + f(1, y-1);

        f(x, y) = call_count(f(x, y));

        Func g("g");
        g(x, y) = f(x, y) + f(x, y-1) + f(x, y-2);

        f.store_root().compute_at(g, y);

        counter = 0;
        g.realize(2, 10);

        if (counter != 60) {
            printf("Failed sliding forwards on a backwards looking reduction update: %d\n", counter);
            return -1;
        }
    }

    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    {
        Func f;

        RDom r(1, 10);
        f(x) = 1;
        // this does not race, because the RDom does not contain 0
        r.where(f(0) == 1);
        f(r) = 2;

        f.update().parallel(r);
    }

    {
        Func f;

        RDom r(0, 10);
        f(x) = 1;
        // this does not race, because there is no communication
        r.where(f(r) == 1);
        f(r) = 2;

        f.update().parallel(r);
    }

    {
        Func f;

        RDom r(0, 10);
        f(x) = 1;
        // this does not race, because there is no communication (odds vs evens)
        r.where(f(2 * r) == 1);
        f(2 * r + 1) = 2;

        f.update().parallel(r);
    }

    printf("Success!\n");
    return 0;
}

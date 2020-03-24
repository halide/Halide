#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

void check(const Buffer<int> &result) {
    for (int x = 0; x < result.width(); x++) {
        int correct[] = {x, 456, 789, 789};
        for (int c = 0; c < 4; c++) {
            if (result(x, c) != correct[c]) {
                printf("result(%d, 0) = %d instead of %d\n",
                       x, result(x, c), correct[c]);
                abort();
            }
        }
    }
}

int main(int argc, char **argv) {
    Var x("x"), c("c");
    {
        Func f("f");

        f(x, c) = mux(c, {x, 456, 789});

        Buffer<int> result = f.realize(100, 4);
        check(result);
    }

    {
        Func f;
        f(x) = {x, 456, 789};
        Func g;
        g(x, c) = mux(c, f(x));

        Buffer<int> result = g.realize(100, 4);
        check(result);
    }

    printf("Success!\n");
    return 0;
}

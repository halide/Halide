#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    Var x;
    // Use a likely intrinsic to tag a disjoint range.
    f(x) = select(x < 10 || x > 20, likely(1), 2);

    Buffer<int> im = f.realize({30});
    for (int x = 0; x < 30; x++) {
        int correct = (x < 10 || x > 20) ? 1 : 2;
        if (im(x) != correct) {
            printf("im(%d) = %d instead of %d\n", x, im(x), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

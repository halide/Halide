#include "Halide.h"
#include <stdio.h>
using namespace Halide;


int main(int argc, char *argv[]) {
    Func sum("sum"), foo("foo");
    Var x("x"), y("y"), c("c");

    RDom r(1, 2, "r");

    // sum(x, y) should equal 3
    sum(x, y) += r.x;

    foo(x, y, c) = select(c == 3, 255, sum(x, y));
    // foo(x, y, c) should equal (3, 3, 3, 255);

    foo.vectorize(c, 4);

    Image<int32_t> output = foo.realize(2, 2, 4);
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            for (int c = 0; c < 4; c++) {
                int correct = (c == 3 ? 255 : 3);
                if (output(x, y, c) != correct) {
                    printf("output(%d, %d, %d) = %d instead of %d\n",
                           x, y, c, output(x, y, c), correct);
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

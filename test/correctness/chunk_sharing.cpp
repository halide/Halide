#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), i("i"), j("j");
    Func a("a"), b("b"), c("c"), d("d");

    printf("Defining function...\n");

    a(i, j) = i + j;
    b(i, j) = a(i, j) + 1;
    c(i, j) = a(i, j) * 2;
    d(x, y) = b(x, y) + c(x, y);

    c.compute_at(d, y);
    b.compute_at(d, y);
    a.compute_at(d, y);

    printf("Realizing function...\n");

    Buffer<int> im = d.realize({32, 32});

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            int a = x + y;
            int b = a + 1;
            int c = a * 2;
            int d = b + c;
            if (im(x, y) != d) {
                printf("im(%d, %d) = %d instead of %d\n", x, y, im(x, y), d);
                return 1;
            }
        }
    }
    printf("Success!\n");
    return 0;
}

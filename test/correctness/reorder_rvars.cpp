#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y");

    {
        RDom r1(0, 10, 1, 10);
        RDom r2(0, 10, 3, 10, 0, 5);

        // Define two identical functions

        Func f("f");
        f(x, y) = x + y;
        f(x, y) += r1.x * r1.y;
        f(x, r2.x) -= r2.z * f(x, r2.x + r2.y);

        Func g("g");
        g(x, y) = x + y;
        g(x, y) += r1.x * r1.y;
        g(x, r2.x) -= r2.z * g(x, r2.x + r2.y);

        // Reorder g
        g.reorder(y, x);
        // It is legal to reorder r1.x and r1.y
        // because stage g.update(0) is associative.
        g.update(0).reorder(r1.y, y, x, r1.x);
        g.update(1).reorder(r2.x, x, r2.y, r2.z);
        g.compute_root();
        f.compute_root();

        RDom r3(0, 20, 0, 20);
        Expr check = sum(abs(f(r3.x, r3.y) - g(r3.x, r3.y)));

        int err = evaluate_may_gpu<int>(cast<int>(check));

        if (err != 0) {
            printf("Reordering rvars affected the meaning!\n");
            return 1;
        }
    }

    // And now, a practical use-case for reorder rvars
    {
        Func input;
        input(x, y) = x * y;

        // Compute summed-area table
        Func sat;
        sat(x, y) = input(x, y);

        RDom r(1, 99);
        sat(x, r) += sat(x, r - 1);
        sat(r, y) += sat(r - 1, y);

        // Walk down the columns in vectors.
        Var xo, xi;
        sat.update().split(x, xo, xi, 4).reorder(xi, r, xo).vectorize(xi).parallel(xo);

        // Walk along the rows in parallel. For this we want the loop
        // over y outside of the loop over r, which is the default.
        sat.update(1).parallel(y);

        sat.realize({100, 100});
    }

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    {
        Func f, g;
        Var x;
        f(x) = Tuple(x, sin(x));
        // If you have c++11, you can just say {x, sin(x)}, instead of
        // Tuple(x, sin(x)).

        f.compute_root();

        Tuple t = f(x);
        g(x) = t[0] + t[1];

        g.realize(100);
    }

    // Now try a reduction
    {
        Func f, g;
        Var x, y;
        f(x, y) = sin(x*y);
        f.compute_root();

        // Find argmax of f over [0, 100]^2
        RDom r(0, 100, 0, 100);

        g() = Tuple(0, 0, f(0, 0));

        Expr best_x = g()[0], best_y = g()[1], best_so_far = g()[2];
        Expr next_value = f(r.x, r.y);
        g() = tuple_select(next_value > best_so_far,
                           Tuple(r.x, r.y, next_value),
                           Tuple(best_x, best_y, best_so_far));

        Realization result = g.realize();
        // int result_x = Image<int>(result[0])(0);
        // int result_y = Image<int>(result[1])(0);
        float result_val = Image<float>(result[2])(0);
        if (result_val < 0.9999) {
            printf("Argmax of sin(x*y) is underwhelming: %f. We expected it to be closer to one.\n", result_val);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}



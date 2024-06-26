#include "Halide.h"
#include <stdio.h>

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(x + y, undef<int32_t>());
        f(x, y)[0] += 3;
        f(x, y)[1] = x;
        f(x, y)[0] -= 1;
        f(x, y)[1] *= 4;
        f(x, y)[1] /= 2;

        Realization result = f.realize({1024, 1024});
        Buffer<int> a = result[0], b = result[1];
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = x + y + 2;
                int correct_b = x * 2;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    {
        Var x("x"), y("y");
        Func f("f");

        f(x, y) = Tuple(x, y);
        f(x, y)[1] += select(x < 20, 20 * x, undef<int>());

        Realization result = f.realize({1024, 1024});
        Buffer<int> a = result[0], b = result[1];
        for (int y = 0; y < a.height(); y++) {
            for (int x = 0; x < a.width(); x++) {
                int correct_a = x;
                int correct_b = (x < 20) ? 20 * x + y : y;
                if (a(x, y) != correct_a || b(x, y) != correct_b) {
                    printf("result(%d, %d) = (%d, %d) instead of (%d, %d)\n",
                           x, y, a(x, y), b(x, y), correct_a, correct_b);
                    return 1;
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}

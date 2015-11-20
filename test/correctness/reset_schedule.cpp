#include "Halide.h"

using namespace Halide;



int main(int argc, char **argv) {
    Func f;
    Var x;
    f(x) = x+3;

    Var xo, xi;

    f.vectorize(x, 8);
    Image<int> v1 = f.realize(100);

    // We can't double-vectorize, so if the schedule wasn't getting reset, this would be an error.
    f.reset_schedule();
    f.split(x, xo, xi, 4).vectorize(xi);
    Image<int> v2 = f.realize(100);

    // If we don't reset the schedule, x would no longer exist, so this would be invalid.
    f.reset_schedule();
    f.unroll(x, 4);
    Image<int> v3 = f.realize(100);

    RDom r(0, 100);
    uint32_t err = evaluate<uint32_t>(sum(abs(v1(r) - v2(r)) + abs(v1(r) - v3(r))));
    if (err) {
        printf("Error: the three methods returned different results\n");
        return -1;
    }

    printf("Success!\n");
    return 0;

}

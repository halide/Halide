#include <stdio.h>
#include "Halide.h"
#include "clock.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    ImageParam a(Int(32), 1);
    Image<int> b(1), c(1);
    b(0) = 17;
    c(0) = 0;
    a.set(c);


    double t1, t2;
    t1 = current_time();

    for (int i = 0; i < 100; i++) {
        Func f;
        f(x) = a(x) + b(x);
        f.realize(c);
        assert(c(0) == (i+1)*17);
    }

    t2 = current_time();
    int elapsed = (int)(10.0 * (t2-t1));

    printf("%d us per jit compilation\n", elapsed);

    printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {

    Func f, g;
    Var x, y;

    f(x, y) = 0;

    RDom r(0, 10, 0, 10);
    f(r.x, r.y) += f(r.y, r.x);

    // This schedule should be forbidden, because it causes a race condition.
    f.update().parallel(r.y);

    // We shouldn't reach here, because there should have been a compile error.
    printf("There should have been an error\n");

    return 0;
}

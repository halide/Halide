#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f("f");
    Var x("x");
    RDom r(0, 4, 0, 3, "r");
    RVar rxo("rxo"), rxi("rxi"), fused("fused");
    f(x) = 1;
    f(x) = f(x) * 2 + r.x * 5 + r.y;

    // this fuse reorders rvars despite the update being non-commutative
    f.update().split(r.x, rxo, rxi, 2).fuse(rxi, r.y, fused);

    printf("Success!\n");
    return 0;
}

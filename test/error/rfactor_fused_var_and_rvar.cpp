#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f{"f"};
    RDom r(0, 100);
    Var x{"x"}, y{"y"};
    f(x, y) = 0;
    f(x, y) += r;

    RVar yr{"yr"};
    Var z{"z"};

    // Error: In schedule for f.update(0), can't perform rfactor() on yr because the pure var y is fused into it.
    f.update().fuse(y, r, yr).rfactor(yr, z);

    printf("Success!\n");
    return 0;
}
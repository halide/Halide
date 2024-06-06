#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Func f;
    RDom r(0, 100);
    Var x, y;
    f(x, y) = 0;
    f(x, y) += r;

    RVar yr;
    Var z;
    f.update().fuse(y, r, yr).rfactor(yr, z);

    return 0;
}
#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<int> in(10, 10);

    Func f("f");
    RDom r(0, in.width(), 0, in.height());
    f(r.x, r.y) = in(r.x, r.y) + 2;
    f.realize(in.width(), in.height());

    return 0;
}

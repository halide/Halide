#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // The code below used to not inject appropriate bounds checks.
    // See https://github.com/halide/Halide/issues/7343

    Var x;

    const int size = 1024;

    Func h;
    h(x) = {0, 0};
    RDom r(0, size);
    h(r) = {h(r - 100)[0], 0};

    Var xo, xi;
    h.split(x, xo, xi, 16, TailStrategy::RoundUp);

    Buffer<int> r0(size);
    Buffer<int> r1(size);
    h.realize({r0, r1});

    return 0;
}

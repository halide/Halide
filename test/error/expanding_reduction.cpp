#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Func f, g;
    Var x, y;
    RDom r(0, 100);

    // This reduction recursively expands its bounds. The
    // initialization step will evaluate y from -1 to 101, and the
    // update step will use y from 0 to 100. Faulty bounds inference
    // might get this wrong.

    // This behavior is now disallowed, so this test has been moved
    // into the error category.
    ImageParam input(Int(32), 2);
    f(x, y) = input(x, y);
    f(r, y) = f(r, y-1) + f(r, y+1);

    f.compute_root();

    g(x, y) = f(x, y);

    g.infer_input_bounds(100, 100);

    Buffer<int> in(input.get());
    assert(in.height() == 102 && in.width() == 100);

    return 0;
}

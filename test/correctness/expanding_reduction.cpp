#include <Halide.h>
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

    ImageParam input(Int(32), 2);
    // This stage gets evaluated over [-1, 100]x[-1, 100]
    f(x, y) = input(x, y);

    // This stage is evaluated over [0, 0]x[0, 99], but it really
    // should be evaluated over [0, 0]x[-1, 100] to satisfy the next
    // stage, but currently all the update stages use the bounds
    // required. See https://github.com/halide/Halide/issues/207
    f(0, y) = f(y-1, y) + f(y+1, y);

    // This stage is evaluated over [0, 99]x[0, 0]
    f(x, 0) = f(x, x-1) + f(x, x+1);

    f.compute_root();

    g(x, y) = f(x, y);

    g.infer_input_bounds(100, 100);

    Image<int> in(input.get());
    assert(in.height() == 102 && in.width() == 102);

    printf("Success!\n");
    return 0;
}

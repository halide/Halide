#include <stdio.h>
#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Func pred("pred");
    pred(x, y) = x < y;

    Func selector("selector");
    selector(x, y) = select(pred(x, y), 1, 0);

    // Load a vector of 16 bools
    // TODO: should be 8, but working around https://llvm.org/bugs/show_bug.cgi?id=30947
    pred.compute_root();
    selector.compute_root().vectorize(x, 16);

    Func result;
    RDom range(0, 100, 0, 100);
    result() = sum(selector(range.x, range.y));

    int32_t r = evaluate_may_gpu<int32_t>(result());

    assert(r == 4950);

    printf("Success!\n");
    return 0;
}

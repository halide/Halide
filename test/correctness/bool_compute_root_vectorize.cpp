#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Var x, y;

    Func pred("pred");
    pred(x, y) = x < y;

    Func selector("selector");
    selector(x, y) = select(pred(x, y), 1, 0);

    // Load a vector of 8 bools
    pred.compute_root();
    selector.compute_root().vectorize(x, 8);

    RDom range(0, 100, 0, 100);
    int32_t result = evaluate_may_gpu<int32_t>(sum(selector(range.x, range.y)));

    assert(result == 4950);

    printf("Success!\n");
    return 0;
}

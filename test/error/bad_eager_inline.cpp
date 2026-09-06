#include "Halide.h"
using namespace Halide;

int main(int argc, char **argv) {
    Var x{"x"};
    RDom r(0, 4);
    Func reduced{"reduced"}, consumer{"consumer"};
    reduced(x) = 0;
    reduced(x) += r;  // update definition -> not pure
    consumer(x) = reduced(x);

    // A Func with an update definition is not inlinable, so eager_inline() rejects it.
    consumer.eager_inline({reduced});

    printf("Success!\n");
    return 0;
}

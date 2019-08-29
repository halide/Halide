#include "Halide.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    Buffer<float> b(10);
    Func f("f"), g("g");
    Var x("x");
    Buffer<int> h(10);
    RDom r(h);

    f(x) = b(clamp(x, 0, 10));
    g() += f(h(r));
    Derivative d = propagate_adjoints(g); // access to f is unbounded
    return 0;
}

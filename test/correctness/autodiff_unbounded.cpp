#include "Halide.h"
#include "expect_user_error.h"
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    return error_test("autodiff_unbounded", "f is accessed over an unbounded domain in dimension x", []() {
        Buffer<float> b(10);
        Func f("f"), g("g");
        Var x("x");
        Buffer<int> h(10);
        RDom r(h);

        f(x) = b(clamp(x, 0, 10));
        g() += f(h(r));
        Derivative d = propagate_adjoints(g);  // access to f is unbounded
        (void)d;
    });
}

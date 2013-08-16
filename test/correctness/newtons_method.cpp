#include <Halide.h>
#include <stdio.h>

using namespace Halide;

int main(int argc, char **argv) {
    // We're going to compute pi, by finding a zero-crossing of sin near 3 using Newton's method.
    Func f;
    f() = cast<double>(3);

    Expr value = sin(f());
    Expr deriv = cos(f());

    // 10 steps is more than sufficient for double precision
    RDom r(0, 10);
    // We have to introduce a dummy dependence on r, because the iteration domain isn't otherwise referenced.
    f() -= value/deriv + (r*0);

    double newton_result = evaluate<double>(f());

    // Now try the secant method, starting with an estimate on either
    // side of 3. We'll reduce onto four values tracking the interval
    // that contains the zero.
    Func g;
    g() = Tuple(cast<double>(3), sin(cast<double>(3)),
                cast<double>(4), sin(cast<double>(4)));

    Expr x1 = g()[0], y1 = g()[1];
    Expr x2 = g()[2], y2 = g()[3];
    Expr x0 = x1 - y1 * (x1 - x2) / (y1 - y2);

    // Stop when the baseline gets too small.
    Expr baseline = abs(y2 - y1);
    x0 = select(baseline > 0, x0, x1);

    // Introduce a dummy dependence on r
    x0 += r*0;

    Expr y0 = sin(x0);

    g() = Tuple(x0, y0, x1, y1);

    double secant_result = evaluate<double>(g()[0]);

    double correct = M_PI;
    if (newton_result != correct ||
        secant_result != correct) {
        printf("Incorrection results: %10.20f %10.20f %10.20f\n",
               newton_result, secant_result, correct);
        return -1;
    }

    printf("Success!\n");

    return 0;
}

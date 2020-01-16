#include "Halide.h"
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif

using namespace Halide;

template<typename T>
int find_pi() {
    // Skip test if data type is not supported by the target.
    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::OpenCL) &&
        !target.has_feature(Target::CLDoubles) &&
        type_of<T>() == type_of<double>()) {
        return 0;
    }

    // We're going to compute pi, by finding a zero-crossing of sin near 3 using Newton's method.
    Func f;
    f() = cast<T>(3);

    Expr value = sin(f());
    Expr deriv = cos(f());

    // 10 steps is more than sufficient for double precision
    RDom r(0, 10);
    // We have to introduce a dummy dependence on r, because the iteration domain isn't otherwise referenced.
    f() -= value / deriv + (r * 0);

    T newton_result = evaluate_may_gpu<T>(f());

    // Now try the secant method, starting with an estimate on either
    // side of 3. We'll reduce onto four values tracking the interval
    // that contains the zero.
    Func g;
    g() = Tuple(cast<T>(3), sin(cast<T>(3)),
                cast<T>(4), sin(cast<T>(4)));

    Expr x1 = g()[0], y1 = g()[1];
    Expr x2 = g()[2], y2 = g()[3];
    Expr x0 = x1 - y1 * (x1 - x2) / (y1 - y2);

    // Stop when the baseline gets too small.
    Expr baseline = abs(y2 - y1);
    x0 = select(baseline > 0, x0, x1);

    // Introduce a dummy dependence on r
    x0 += r * 0;

    Expr y0 = sin(x0);

    g() = Tuple(x0, y0, x1, y1);

    T secant_result = evaluate_may_gpu<T>(g()[0]);

    T correct = (T)M_PI;
    if (newton_result != correct ||
        secant_result != correct) {
        printf("Incorrect results: %10.20f %10.20f %10.20f\n",
               newton_result, secant_result, correct);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int result;

    // Test in float.
    result = find_pi<float>();
    if (result != 0)
        return result;

    if (get_jit_target_from_environment().supports_type(type_of<double>())) {
        result = find_pi<double>();
        if (result != 0)
            return result;
    }

    printf("Success!\n");
    return 0;
}

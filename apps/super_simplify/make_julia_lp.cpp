#include "Halide.h"

#include <iostream>

using namespace Halide;
#include "ExprToJuliaLP.h"
#include "generate_bounds_cegis.h" // make_symbolic_scope()
#include "tropical_optimization.h"

using Halide::Internal::Interval;
using Halide::Var;
using Halide::Expr;

int main(int argc, char **argv) {
    Var x("x"), e("e");
    // Expr test = (min((x*10) + 10, e) - max(min(x*10, e), min(e, 10) + -11));
    Expr test = (min(x, e + -16) - max(min(x, e), min(e, 16) + -16));
    Expr convex_test = pullMinMaxOutermost(test);

    Interval interval = find_constant_bounds(test, make_symbolic_scope(test));
    Interval interval_convex = find_constant_bounds(test, make_symbolic_scope(test));

    std::cerr << test << " -> " << convex_test << std::endl;

    std::cerr << "[ " << interval.min << ", " << interval.max << " ]" << std::endl;
    std::cerr << "[ " << interval_convex.min << ", " << interval_convex.max << " ]" << std::endl;

    Expr expr = test;
    bool upper = true;

    std::string lp = expr_to_julia_lp(expr, upper);

    std::cerr << "# Making LP for expression: " << expr << "\n";
    std::cerr << lp << std::endl;

    return 0;
}
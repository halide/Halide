#include "Halide.h"

#include <iostream>

using namespace Halide;
#include "ExprToJuliaLP.h"
#include "generate_bounds_cegis.h" // make_symbolic_scope()

using Halide::Internal::Interval;
using Halide::Var;
using Halide::Expr;

int main(int argc, char **argv) {
    Var x("x"), e("e");
    Expr test = (min((x*10) + 10, e) - select(0 < x, min(x*10, e), (x*10) + -1));

    Interval interval = find_constant_bounds(test, make_symbolic_scope(test));

    std::cerr << "[ " << interval.min << ", " << interval.max << " ]" << std::endl;

    std::string lp = expr_to_julia_lp(test, true);

    std::cerr << lp << std::endl;

    return 0;
}
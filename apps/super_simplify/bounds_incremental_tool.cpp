#include "Halide.h"

#include <iostream>

#include "bounds_incremental.h"
#include "generate_bounds_cegis.h" // make_symbolic_scope()

using Halide::Internal::Interval;
using Halide::Var;
using Halide::Expr;
using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), e("e");

    // Expr test = max(x, -999) - min(x, 1999);
    Expr test = x * y;

    Interval interval = Halide::Internal::bounds_of_expr_in_scope(test, make_symbolic_scope(test));

    std::cerr << "lower:" << interval.min << "\n";
    std::cerr << "upper:" << interval.max << "\n";
    // return 1;


    const bool upper = true;
    const int max_size = 8;

    for (int i = 0; i < max_size; i++) {
        Expr res = generate_bounds_incremental(test, upper, i);
        if (res.defined()) {
            std::cout << "Found bound:" << res << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to find bound on round: " << i << std::endl;
        }
    }
    return 1;
}
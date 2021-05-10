#include "Halide.h"

#include <iostream>

#include "bounds_simplify.h"

using Halide::Internal::Interval;
using Halide::Var;
using Halide::Expr;
using namespace Halide;

int main(int argc, char **argv) {
    Var x("x"), y("y"), e("e");

    // Expr test = x + x;
    // (let t511.s = min((casted.s0.x.x*1000), (casted.extent.0 + -1000)) in (max((casted.min.0 + t511.s), -999) - min((casted.min.0 + t511.s), 1999)))
    // let a = min(y * 1000, z - 1000) in (max(w + a, -999) - min(w + a, 1999))
    // Expr test = (min((x*10) + 10, e) - select(0 < x, min(x*10, e), (x*10) + -1));
    Expr test = max(x, -999) - min(x, 1999);

    Interval interval = Halide::Internal::find_constant_bounds(test, Internal::Scope<Interval>::empty_scope());

    std::cerr << "lower:" << interval.min << "\n";
    std::cerr << "upper:" << interval.max << "\n";
    const bool upper = true;
    const int max_size = 8;

    for (int i = 0; i < max_size; i++) {
        Expr res = bounds_simplify(test, upper, i);
        if (res.defined()) {
            std::cout << "Found bound:" << res << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to find bound on round: " << i << std::endl;
        }
    }
    return 1;
}
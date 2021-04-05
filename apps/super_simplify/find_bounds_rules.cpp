#include "Halide.h"

#include <iostream>

#include "expr_util.h"
#include "parser.h"
#include "reduction_order.h"
#include "super_simplify.h"
#include "synthesize_predicate.h"
#include "generate_bounds_cegis.h"

using Halide::Internal::Interval;
using Halide::Var;
using Halide::Expr;

int main(int argc, char **argv) {
    Var x("x"), y("y"), e("e");

    Expr test = x + y;

    Interval interval = Halide::Internal::bounds_of_expr_in_scope(test, make_symbolic_scope(test));

    bool upper = true;

    int max_leaf_count = 0;

    if (upper) {
        std::cerr << "max: " << interval.max << std::endl;
        max_leaf_count = count_leaves(interval.max);
    } else {
        std::cerr << "max: " << interval.min << std::endl;
        max_leaf_count = count_leaves(interval.min);
    }

    std::cerr << "# leaves: " << max_leaf_count << std::endl;

    // We want a bound with less leaves, so start looking for smaller bounds:
    // it's unlikely that we find a bound smaller than the size of the program (other than a constant), so stop looking there.
    int min_leaf_count = count_leaves(test);

    min_leaf_count = std::min(min_leaf_count, 2);

    max_leaf_count = std::max(max_leaf_count, min_leaf_count + 5);

    std::cerr << "min leaves: " << min_leaf_count << std::endl;

    for (int i = 0; i < max_leaf_count; i++) {
        Expr res = generate_bound(test, upper, i, max_leaf_count);
        if (res.defined()) {
            std::cout << "Found upper bound:" << res << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to find UB on round: " << i << std::endl;
        }
    }
    return 1;
}
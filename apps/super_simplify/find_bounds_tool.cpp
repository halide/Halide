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

    // TODO: lower bounds too
    if (argc != 4) {
        std::cout << "Usage: ./find_bounds_tool halide_exprs.txt max_size (upper | lower)\n";
        return 0;
    }

    std::vector<Expr> exprs = parse_halide_exprs_from_file(argv[1]);
    const int max_size = std::atoi(argv[2]);
    std::string flag = argv[3];
    bool upper = true;
    if (flag == "lower") {
        upper = false;
    } else if (flag != "upper") {
        std::cerr << "do not recognize upper/lower bound flag: " << flag << std::endl;
        return 1;
    }


    for (auto e : exprs) {
        Interval interval = Halide::Internal::bounds_of_expr_in_scope(e, make_symbolic_scope(e));
        int max_leaf_count = (upper) ? count_leaves(interval.max) : count_leaves(interval.min);

        Expr res = generate_bound(e, upper, max_size, max_leaf_count);
        if (res.defined()) {
            std::cout << "Found bound:" << e << " -> " << res << std::endl;
        } else {
            std::cerr << "Failed to find UB on expr: " << e << std::endl;
        }
    }
    return 0;
}
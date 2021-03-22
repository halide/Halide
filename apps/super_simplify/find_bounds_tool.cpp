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


Expr find_bound(Expr test, int max_size, int max_leaf_count, bool upper) {
    for (int i = 0; i < max_size; i++) {
        Expr res = generate_bound(test, upper, i, max_leaf_count);
        if (res.defined()) {
            std::cout << "Found upper bound:" << res << std::endl;
            return res;
        } else {
            std::cerr << "Failed to find UB on round: " << i << std::endl;
        }
    }
    return Expr();
}

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
        // TODO(rootjalex): just fix this:
        max_leaf_count++;
        std::cerr << "# leaves: " << max_leaf_count << std::endl;
        Expr ebound = find_bound(e, max_size, max_leaf_count, upper);
    }

    return 0;
}
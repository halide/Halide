#include "Halide.h"

#include <iostream>

#include "expr_util.h"
#include "parser.h"
#include "reduction_order.h"
#include "super_simplify.h"
#include "synthesize_predicate.h"
#include "generate_bounds_cegis.h"

int main(int argc, char **argv) {

    Halide::Var x("x"), y("y");

    Halide::Expr test = select(x > y, y, x);

    const int max_size = 6;

    for (int i = 0; i <= max_size; i++) {
        Halide::Expr res = generate_bound(test, true, i);
        if (res.defined()) {
            std::cout << "Found upper bound:" << res << std::endl;
            return 0;
        } else {
            std::cerr << "Failed to find UB on round: " << i << std::endl;
        }
    }

    return 1;
}
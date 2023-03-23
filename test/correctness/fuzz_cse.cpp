#include "Halide.h"

#include <time.h>

using namespace Halide;
using namespace Halide::Internal;
using std::vector;

std::mt19937 rng(0);

Expr random_expr(int depth, vector<Expr> &exprs) {
    if (depth <= 0) {
        return (int)(rng() % 10 - 5);
    }

    if (!exprs.empty() && (rng() & 1)) {
        // Reuse an existing expression
        return exprs[rng() % exprs.size()];
    }

    Expr next;
    switch (rng() % 9) {
    case 0:
        next = Var("x");
        break;
    case 1:
        next = Var("y");
        break;
    case 2:
        next = Var("z");
        break;
    case 3:
        // Any binary op is equally good
        next = random_expr(depth - 1, exprs);
        next += random_expr(depth - 1, exprs);
        break;
    case 4: {
        Expr a = random_expr(depth - 2, exprs);
        Expr b = random_expr(depth - 2, exprs);
        Expr c = random_expr(depth - 2, exprs);
        Expr d = random_expr(depth - 2, exprs);
        next = select(a > b, c, d);
        break;
    }
    case 5: {
        Expr a = random_expr(depth - 1, exprs);
        Expr b = random_expr(depth - 1, exprs);
        next = Let::make("x", a, b);
        break;
    }
    case 6: {
        Expr a = random_expr(depth - 1, exprs);
        Expr b = random_expr(depth - 1, exprs);
        next = Let::make("y", a, b);
        break;
    }
    case 7: {
        Expr a = random_expr(depth - 1, exprs);
        Expr b = random_expr(depth - 1, exprs);
        next = Let::make("z", a, b);
        break;
    }
    default:
        next = (int)(rng() % 10 - 5);
    }
    exprs.push_back(next);
    return next;
}

int main(int argc, char **argv) {
    int fuzz_seed = argc > 1 ? atoi(argv[1]) : time(nullptr);

    for (int i = 0; i < 10000; i++) {
        rng.seed(fuzz_seed);
        vector<Expr> exprs;
        Expr orig = random_expr(5, exprs);

        Expr csed = common_subexpression_elimination(orig);

        Expr check = (orig == csed);
        check = Let::make("x", 1, check);
        check = Let::make("y", 2, check);
        check = Let::make("z", 3, check);
        Stmt check_stmt = uniquify_variable_names(Evaluate::make(check));
        check = check_stmt.as<Evaluate>()->value;

        // Don't use can_prove, because it recursively calls cse, which just confuses matters.
        if (!is_const_one(simplify(check))) {
            std::cerr << "Mismatch with seed " << fuzz_seed << "\n"
                      << "Original: " << orig << "\n"
                      << "CSE: " << csed << "\n";
            return 1;
        }
        fuzz_seed = rng();
    }

    std::cout << "Success!\n";
    return 0;
}

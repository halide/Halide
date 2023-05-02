#include "Halide.h"

#include <fuzzer/FuzzedDataProvider.h>
#include <time.h>

using namespace Halide;
using namespace Halide::Internal;
using std::vector;

Expr random_expr(FuzzedDataProvider &fdp, int depth, vector<Expr> &exprs) {
    if (depth <= 0) {
        return fdp.ConsumeIntegralInRange<int>(-5, 4);
    }

    if (!exprs.empty() && fdp.ConsumeBool()) {
        // Reuse an existing expression
        return exprs[fdp.ConsumeIntegralInRange<size_t>(0, exprs.size() - 1)];
    }

    Expr next;
    switch (fdp.ConsumeIntegralInRange<int>(0, 8)) {
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
        next = random_expr(fdp, depth - 1, exprs);
        next += random_expr(fdp, depth - 1, exprs);
        break;
    case 4: {
        Expr a = random_expr(fdp, depth - 2, exprs);
        Expr b = random_expr(fdp, depth - 2, exprs);
        Expr c = random_expr(fdp, depth - 2, exprs);
        Expr d = random_expr(fdp, depth - 2, exprs);
        next = select(a > b, c, d);
        break;
    }
    case 5: {
        Expr a = random_expr(fdp, depth - 1, exprs);
        Expr b = random_expr(fdp, depth - 1, exprs);
        next = Let::make("x", a, b);
        break;
    }
    case 6: {
        Expr a = random_expr(fdp, depth - 1, exprs);
        Expr b = random_expr(fdp, depth - 1, exprs);
        next = Let::make("y", a, b);
        break;
    }
    case 7: {
        Expr a = random_expr(fdp, depth - 1, exprs);
        Expr b = random_expr(fdp, depth - 1, exprs);
        next = Let::make("z", a, b);
        break;
    }
    default:
        next = fdp.ConsumeIntegralInRange<int>(-5, 4);
    }
    exprs.push_back(next);
    return next;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider fdp(data, size);
    vector<Expr> exprs;
    Expr orig = random_expr(fdp, 5, exprs);

    Expr csed = common_subexpression_elimination(orig);

    Expr check = (orig == csed);
    check = Let::make("x", 1, check);
    check = Let::make("y", 2, check);
    check = Let::make("z", 3, check);
    Stmt check_stmt = uniquify_variable_names(Evaluate::make(check));
    check = check_stmt.as<Evaluate>()->value;

    // Don't use can_prove, because it recursively calls cse, which just confuses matters.
    assert(is_const_one(simplify(check)));

    return 0;
}

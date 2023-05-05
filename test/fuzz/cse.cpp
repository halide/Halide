#include "Halide.h"

#include "fuzz_helpers.h"
#include <functional>
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
        return pick_value_in_vector(fdp, exprs);
    }
    std::function<Expr()> build_next_expr[] = {
        [&]() {
            return Var("x");
        },
        [&]() {
            return Var("y");
        },
        [&]() {
            return Var("z");
        },
        [&]() {
            Expr next = random_expr(fdp, depth - 1, exprs);
            next += random_expr(fdp, depth - 1, exprs);
            return next;
        },
        [&]() {
            Expr a = random_expr(fdp, depth - 2, exprs);
            Expr b = random_expr(fdp, depth - 2, exprs);
            Expr c = random_expr(fdp, depth - 2, exprs);
            Expr d = random_expr(fdp, depth - 2, exprs);
            return select(a > b, c, d);
        },
        [&]() {
            Expr a = random_expr(fdp, depth - 1, exprs);
            Expr b = random_expr(fdp, depth - 1, exprs);
            return Let::make("x", a, b);
        },
        [&]() {
            Expr a = random_expr(fdp, depth - 1, exprs);
            Expr b = random_expr(fdp, depth - 1, exprs);
            return Let::make("y", a, b);
        },
        [&]() {
            Expr a = random_expr(fdp, depth - 1, exprs);
            Expr b = random_expr(fdp, depth - 1, exprs);
            return Let::make("z", a, b);
        },
        [&]() {
            return fdp.ConsumeIntegralInRange<int>(-5, 4);
        },
    };
    Expr next = fdp.PickValueInArray(build_next_expr)();
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

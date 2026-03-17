#include "Halide.h"

#include "fuzz_helpers.h"
#include <functional>
#include <time.h>

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace Halide::Internal;
using std::pair;
using std::vector;

// Note that this deliberately uses int16 values everywhere --
// *not* int32 -- because we want to test CSE, not the simplifier's
// overflow behavior, and using int32 can end up with results
// containing signed_integer_overflow(), which is not helpful here.
Expr random_expr(FuzzingContext &fuzz, int depth, vector<pair<Expr, int>> &exprs) {
    if (depth <= 0) {
        return i16(fuzz.ConsumeIntegralInRange<int>(-5, 4));
    }
    if (!exprs.empty() && fuzz.ConsumeBool()) {
        // Reuse an existing expression that was generated under conditions at
        // least as strict as our current depth limit.
        auto p = fuzz.PickValueInVector(exprs);
        if (p.second <= depth) {
            return p.first;
        }
    }
    std::function<Expr()> build_next_expr[] = {
        [&]() {
        // Can't use Var() here because that would require i32 values,
        // which we are avoiding here because we don't want to end
        // up with signed_integer_overflow()
        return Variable::make(Int(16), "x");
    },
        [&]() {
        return Variable::make(Int(16), "y");
    },
        [&]() {
        return Variable::make(Int(16), "z");
    },
        [&]() {
        Expr next = random_expr(fuzz, depth - 1, exprs);
        next += random_expr(fuzz, depth - 1, exprs);
        return next;
    },
        [&]() {
        Expr a = random_expr(fuzz, depth - 2, exprs);
        Expr b = random_expr(fuzz, depth - 2, exprs);
        Expr c = random_expr(fuzz, depth - 2, exprs);
        Expr d = random_expr(fuzz, depth - 2, exprs);
        return select(a > b, c, d);
    },
        [&]() {
        Expr a = random_expr(fuzz, depth - 1, exprs);
        Expr b = random_expr(fuzz, depth - 1, exprs);
        return i16(Let::make("x", a, b));
    },
        [&]() {
        Expr a = random_expr(fuzz, depth - 1, exprs);
        Expr b = random_expr(fuzz, depth - 1, exprs);
        return i16(Let::make("y", a, b));
    },
        [&]() {
        Expr a = random_expr(fuzz, depth - 1, exprs);
        Expr b = random_expr(fuzz, depth - 1, exprs);
        return i16(Let::make("z", a, b));
    },
        [&]() {
        return i16(fuzz.ConsumeIntegralInRange<int>(-5, 4));
    },
    };
    Expr next = fuzz.PickValueInArray(build_next_expr)();
    exprs.emplace_back(next, depth);
    return next;
}

FUZZ_TEST(cse, FuzzingContext &fuzz) {
    vector<pair<Expr, int>> exprs;
    Expr orig = random_expr(fuzz, 5, exprs);

    Expr csed = common_subexpression_elimination(orig);

    Expr check = (orig == csed);
    check = Let::make("x", i16(1), check);
    check = Let::make("y", i16(2), check);
    check = Let::make("z", i16(3), check);
    Stmt check_stmt = uniquify_variable_names(Evaluate::make(check));
    check = check_stmt.as<Evaluate>()->value;

    // Don't use can_prove, because it recursively calls cse, which just confuses matters.
    Expr result = simplify(check);
    if (is_const_one(result)) {
        return 0;
    }

    return 1;
}

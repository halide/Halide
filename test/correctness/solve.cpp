#include "Halide.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Assert that solve_expression produces exactly the given expected expression.
void check_solve(const Expr &in, const Expr &expected) {
    SolverResult solved = solve_expression(in, "x");
    if (!equal(solved.result, expected)) {
        std::cerr << "solve_expression produced unexpected result:\n"
                  << "  input:    " << in << "\n"
                  << "  expected: " << expected << "\n"
                  << "  actual:   " << solved.result << "\n";
        std::abort();
    }
}

// Assert that solve_expression produces a result that is semantically
// equivalent to the input under the given substitution. This is used for
// cases where we care about preserved meaning, not exact syntactic form.
void check_solve_equivalent(const Expr &in, const std::map<std::string, Expr> &vars) {
    SolverResult solved = solve_expression(in, "x");
    Expr in_v = simplify(substitute(vars, in));
    Expr out_v = simplify(substitute(vars, solved.result));
    if (!equal(in_v, out_v)) {
        std::cerr << "solve_expression changed value under substitution:\n"
                  << "  input:    " << in << "\n"
                  << "  solved:   " << solved.result << "\n";
        for (const auto &[name, val] : vars) {
            std::cerr << "  " << name << " = " << val << "\n";
        }
        std::cerr << "  input evaluated:  " << in_v << "\n"
                  << "  solved evaluated: " << out_v << "\n";
        std::abort();
    }
}

// Bug #1: the solver was rewriting `f(x) + b @ c` to `f(x) @ c - b` for
// every comparison @, but for unsigned types the subtraction wraps, which
// does not preserve the *ordering* comparisons LT/LE/GT/GE (the EQ/NE
// rewrite is still valid under modular arithmetic, so those stay).
void test_unsigned_ordering_not_rearranged() {
    Expr x = Variable::make(UInt(32), "x");
    Expr y = Variable::make(UInt(32), "y");

    // A concrete substitution that demonstrates the wrap: with x = 4 and
    // y = (uint32_t)-14 = 4294967282, x + y = 4294967286, so
    // `x + y < 1641646169` is false. The buggy rewrite `x < 1641646169 - y`
    // underflows 1641646169 - 4294967282 to 1641646183, making it true.
    std::map<std::string, Expr> vars{
        {"x", UIntImm::make(UInt(32), 4)},
        {"y", UIntImm::make(UInt(32), 4294967282u)},
    };

    Expr c = UIntImm::make(UInt(32), 1641646169u);
    check_solve_equivalent(x + y < c, vars);
    check_solve_equivalent(x + y <= c, vars);
    check_solve_equivalent(x + y > c, vars);
    check_solve_equivalent(x + y >= c, vars);

    // The symmetric subtraction form must be preserved too.
    check_solve_equivalent(x - y < c, vars);
    check_solve_equivalent(x - y <= c, vars);
    check_solve_equivalent(x - y > c, vars);
    check_solve_equivalent(x - y >= c, vars);
}

// Bug #1 corollary: EQ/NE rewrites are still safe under modular arithmetic
// (modular equivalence preserves equality), so these should continue to be
// rewritten to isolate x on the left.
void test_unsigned_equality_still_rearranged() {
    Expr x = Variable::make(UInt(32), "x");
    Expr y = Variable::make(UInt(32), "y");
    Expr c = UIntImm::make(UInt(32), 2u);

    // `x + y == c` should solve to `x == c - y`, matching existing tests
    // in src/Solve.cpp's solve_test() for unsigned rewrites.
    check_solve(x + y == c, x == (c - y));
    check_solve(x + y != c, x != (c - y));
}

// Bug #2: the solver was rewriting `f(x) * y @ b` to forms involving `b / y`
// and `b % y` even when `y` was a non-constant expression. When `y` evaluates
// to zero the rewrite introduces division-by-zero undefined behavior that was
// not present in the input.
void test_nonconstant_multiplier_not_rewritten() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    // At y = 0, `x * y == 1` is the well-defined `0 == 1 == false`.
    // The buggy rewrite `x == 1/y && 1%y == 0` hits `1/0` and `1%0`, UB.
    std::map<std::string, Expr> vars_zero{
        {"x", Expr(7)},
        {"y", Expr(0)},
    };
    check_solve_equivalent(x * y == 1, vars_zero);
    check_solve_equivalent(x * y != 1, vars_zero);

    // Non-zero y: must still be semantically preserved.
    std::map<std::string, Expr> vars_nonzero{
        {"x", Expr(7)},
        {"y", Expr(3)},
    };
    check_solve_equivalent(x * y == 1, vars_nonzero);
    check_solve_equivalent(x * y != 1, vars_nonzero);
}

// The guarded form of the Mul rewrite -- a positive constant multiplier --
// must continue to work after the fix. visit(Div) constant-folds when both
// operands are const, so `Div::make(7, 3)` reduces to 2 during mutation;
// there is no analogous fold for Mod so the Mod node stays.
void test_positive_const_multiplier_still_rewritten() {
    Expr x = Variable::make(Int(32), "x");
    Expr seven = Expr(7);
    Expr three = Expr(3);
    check_solve(3 * x == 7,
                (x == 2) && (Mod::make(seven, three) == 0));
    check_solve(3 * x != 7,
                (x != 2) || (Mod::make(seven, three) != 0));
}

// Regression test for an unsoundness in bounds_of_expr_in_scope (which
// and_condition_over_domain delegates to). The solver fuzzer surfaced a
// comparison `0 >= int32(min(big_u64, u64(a2) + u64(a0)))` whose weakening
// collapsed to `const_true` -- bounds_of was applying a shortcut for casts
// to signed int32+ that assumed no truncation regardless of source type,
// and the uint64 upper bound (big) wrapped to a negative int32 constant,
// which then satisfied `0 >= that_max` vacuously.
void test_and_condition_sound_for_narrowing_uint_cast() {
    Expr a0 = Variable::make(Int(32), "a0");
    Expr a2 = Variable::make(Int(32), "a2");

    Expr big = UIntImm::make(UInt(64), 18446744073407718471ULL);
    Expr sum = Cast::make(UInt(64), a2) + Cast::make(UInt(64), a0);
    Expr rhs = Cast::make(Int(32), min(big, sum));
    Expr cond = Expr(0) >= rhs;

    Scope<Interval> scope;
    scope.push("a0", Interval(Expr(-5), Expr(14)));
    scope.push("a2", Interval(Expr(9), Expr(16)));

    // At a0=4, a2=11: u64(11)+u64(4) = 15, min(big, 15) = 15, int32(15) = 15,
    // and 0 >= 15 is false. Since cond is false somewhere in the domain,
    // and_condition_over_domain must not return const_true.
    Expr weakened = simplify(and_condition_over_domain(cond, scope));
    if (is_const_one(weakened)) {
        std::cerr << "and_condition_over_domain unsound:\n"
                  << "  cond: " << cond << "\n"
                  << "  weakened: " << weakened << "\n"
                  << "  but cond is false at a0=4, a2=11.\n";
        std::abort();
    }
}

// Second bounds_of_expr_in_scope regression. The shortcut in the Cast
// handler was also blanket-applying to signed-int-to-signed-int narrowing
// with bounded children: `int32(select(cond, int64(uint32(a4)), 59_i64))`
// where a4 is pinned to -9 has child bounds `[59, 4294967287]`. Casting
// those bounds to Int(32) wrapped 4294967287 to -9, producing the inverted
// interval `[59, -9]`. Downstream the comparison `0 < that` was resolved as
// `LHS_max < RHS_min` => `1 < 59` => true, claiming the condition was
// always true even though at cond=true it evaluates to `0 < -9` = false.
void test_and_condition_sound_for_narrowing_int_cast() {
    Expr a4 = Variable::make(Int(32), "a4");
    Expr cond_var = Variable::make(Bool(), "cond");

    Expr rhs = Cast::make(Int(32),
                          Select::make(cond_var,
                                       Cast::make(Int(64), Cast::make(UInt(32), a4)),
                                       IntImm::make(Int(64), 59)));
    Expr cond = Expr(0) < rhs;

    Scope<Interval> scope;
    scope.push("a4", Interval(Expr(-9), Expr(-9)));
    scope.push("cond", Interval(Expr(false), Expr(true)));

    // At cond=true, a4=-9: int64(uint32(-9)) = 4294967287, int32(that) = -9,
    // and 0 < -9 is false.
    Expr weakened = simplify(and_condition_over_domain(cond, scope));
    if (is_const_one(weakened)) {
        std::cerr << "and_condition_over_domain unsound (narrowing int cast):\n"
                  << "  cond: " << cond << "\n"
                  << "  weakened: " << weakened << "\n"
                  << "  but cond is false at cond=true, a4=-9.\n";
        std::abort();
    }
}

// Solver used to rewrite `f(x) + f(x) -> f(x) * 2` via `operator*(Expr, int)`,
// which rejects constants that don't fit in the expression type. For UInt(1),
// the literal 2 isn't representable, aborting the whole solve. Use Mul::make
// directly with make_const (which truncates modulo width) so the rewrite
// applies soundly for every integer type -- for UInt(1), `a * 2` correctly
// becomes `a * 0`, matching the modular value of `a + a`.
void test_solve_does_not_abort_on_narrow_self_add() {
    Expr x = Variable::make(UInt(1), "x");
    // This used to abort with
    //   "Integer constant 2 will be implicitly coerced to type uint1..."
    SolverResult s = solve_expression(x + x, "x");
    // The actual rewritten form is unimportant here -- the test just locks
    // in that solve_expression doesn't abort on this shape.
    if (!s.result.defined()) {
        std::cerr << "solve_expression returned undefined on `x + x` (UInt(1))\n";
        std::abort();
    }
}

// Solver's `f(x)/a + g(x) -> (f(x) + g(x) * a) / a` rewrite is only valid
// under non-wrapping arithmetic: modularly, g(x)*a can overflow the width
// and the rewrite changes the computed value. Guard it on no_overflow_int.
void test_narrow_div_add_equivalence() {
    // Reproduced from the fuzzer (seed 9414558261169807111, minimized):
    // `(uint8(a4)/137) + uint8(a4)` at a4=-13 (uint8 243) is
    //   243/137 + 243 = 1 + 243 = 244 (uint8, no wrap).
    // The previous rewrite would convert this to
    //   (uint8(a4) * 138) / 137
    // which at uint8 243 gives (243*138 mod 256)/137 = 254/137 = 1.
    Expr a = Variable::make(Int(32), "a");
    Expr u = Cast::make(UInt(8), a);
    Expr input = u / UIntImm::make(UInt(8), 137) + u;
    SolverResult s = solve_expression(input, "a");
    // Verify by concrete substitution: the solved expression must evaluate
    // to the same value as the input at a = -13.
    std::map<std::string, Expr> subst{{"a", Expr(-13)}};
    Expr in_v = simplify(substitute(subst, input));
    Expr out_v = simplify(substitute(subst, s.result));
    if (!equal(in_v, out_v)) {
        std::cerr << "solve_expression changed value on narrow div+add:\n"
                  << "  input:  " << input << " -> " << in_v << "\n"
                  << "  solved: " << s.result << " -> " << out_v << "\n";
        std::abort();
    }
}

// bounds_of_expr_in_scope for `float % float` was applying integer-mod
// semantics and claiming the result is always in `[0, max(|b|, -b.min)]`.
// For floats, fmod takes the sign of the dividend, and fmod(x, 0) is NaN,
// so the integer reasoning is unsound. For example the fuzzer generated
//   (float64(a3) % float64(a4)) >= 291249580.0
// with a3 in [-10,-3] and a4 in [-9, 4]; the old bounds were
// [false, false], which propagated up through int32 and min to give a
// comparison that and_condition_over_domain reported as always-true,
// even though at a3=-7, a4=0 the input evaluates to false.
void test_bounds_of_float_mod_is_sound() {
    Expr a = Variable::make(Float(64), "a");
    Expr b = Variable::make(Float(64), "b");
    Expr e = Mod::make(a, b);

    Scope<Interval> scope;
    scope.push("a", Interval(Expr(-10.0), Expr(-3.0)));
    scope.push("b", Interval(Expr(-9.0), Expr(4.0)));

    Interval bounds = bounds_of_expr_in_scope(e, scope);
    // The correct answer for this interval is "we don't know" (NaN is
    // possible when b contains zero). Require the bounds to have no
    // finite lower bound and be either unbounded above, or at least
    // non-negative -- the prior unsound bound was [0, 9].
    if (bounds.has_lower_bound()) {
        Expr provably_nonneg = simplify(bounds.min >= Expr(0.0));
        if (is_const_one(provably_nonneg)) {
            std::cerr << "bounds_of float % float claims result is non-negative, "
                      << "but fmod takes the sign of the dividend:\n"
                      << "  expr: " << e << "\n"
                      << "  bounds.min: " << simplify(bounds.min) << "\n"
                      << "  bounds.max: " << simplify(bounds.max) << "\n";
            std::abort();
        }
    }
}

// Simplify_Cast was applying a cast-chain simplification
//   int32(uint64(X)) -> int32(X)
// whenever widths and the two outer types all lined up for the
// "sign-extend then truncate" shape. The rule's correctness depends on
// the *inner* cast actually being a sign/zero extend, which only holds
// when its source is an integer. For `int32(uint64(float64(a)))` the
// inner cast is an fp-to-uint conversion, which has entirely different
// semantics -- so the stripped form `int32(float64(a))` evaluates to a
// different value (fp-to-int vs fp-to-uint-then-truncate).
// bounds_of was taking a shortcut for `intN(float_expr)` casts (N >= 32) that
// assumed the float-to-signed-int cast "truncates in place and preserves
// interval orientation", so it carried the source float bounds through the
// cast unchanged. But the simplifier folds out-of-range float constants by
// wrapping (IntImm::make sign-extends the low 32 bits), which can invert the
// resulting int interval -- e.g. floats {22e9, 24e9} both wrap, and the two
// wrapped values happen to land on opposite sides of zero. The shortcut then
// produced an inverted (empty) interval, which downstream reasoning treated
// as a vacuously-satisfied constraint.
void test_bounds_of_float_to_int_cast_is_sound() {
    Expr a = Variable::make(Int(32), "a");
    Expr f = -1712582016.0f * cast<float>(a);
    Expr e = cast<int>(f);

    Scope<Interval> scope;
    scope.push("a", Interval(cast<int>(-14), cast<int>(-13)));

    Interval bounds = bounds_of_expr_in_scope(e, scope);

    // Concrete evaluations at each endpoint.
    std::map<std::string, Expr> sub_min{{"a", Expr(-14)}};
    std::map<std::string, Expr> sub_max{{"a", Expr(-13)}};
    Expr v_min = simplify(substitute(sub_min, e));
    Expr v_max = simplify(substitute(sub_max, e));

    // bounds_of must produce an interval that contains both observed values.
    // Either the interval is unbounded on that side, or the bound must
    // provably hold.
    auto must_hold = [&](const Expr &claim, const char *msg) {
        Expr simplified = simplify(claim);
        if (!is_const_one(simplified)) {
            std::cerr << "bounds_of int32(float) unsound: " << msg << "\n"
                      << "  expr:       " << e << "\n"
                      << "  bounds.min: " << simplify(bounds.min) << "\n"
                      << "  bounds.max: " << simplify(bounds.max) << "\n"
                      << "  v(a=-14):   " << v_min << "\n"
                      << "  v(a=-13):   " << v_max << "\n";
            std::abort();
        }
    };

    if (bounds.has_lower_bound()) {
        must_hold(bounds.min <= v_min, "v(a=-14) below bounds.min");
        must_hold(bounds.min <= v_max, "v(a=-13) below bounds.min");
    }
    if (bounds.has_upper_bound()) {
        must_hold(bounds.max >= v_min, "v(a=-14) above bounds.max");
        must_hold(bounds.max >= v_max, "v(a=-13) above bounds.max");
    }
}

void test_simplify_preserves_float_to_uint_cast_chain() {
    Expr a = Variable::make(Int(32), "a");
    Expr chained = Cast::make(Int(32),
                              Cast::make(UInt(64),
                                         Cast::make(Float(64), a)));
    Expr simplified = simplify(chained);

    // At a = -21, the two forms must agree.
    std::map<std::string, Expr> subst{{"a", Expr(-21)}};
    Expr v1 = simplify(substitute(subst, chained));
    Expr v2 = simplify(substitute(subst, simplified));
    if (!equal(v1, v2)) {
        std::cerr << "simplify changed the value of a cast chain:\n"
                  << "  original:   " << chained << " -> " << v1 << "\n"
                  << "  simplified: " << simplified << " -> " << v2 << "\n";
        std::abort();
    }
}

}  // namespace

int main(int argc, char **argv) {
    test_unsigned_ordering_not_rearranged();
    test_unsigned_equality_still_rearranged();
    test_nonconstant_multiplier_not_rewritten();
    test_positive_const_multiplier_still_rewritten();
    test_and_condition_sound_for_narrowing_uint_cast();
    test_and_condition_sound_for_narrowing_int_cast();
    test_solve_does_not_abort_on_narrow_self_add();
    test_narrow_div_add_equivalence();
    test_bounds_of_float_mod_is_sound();
    test_bounds_of_float_to_int_cast_is_sound();
    test_simplify_preserves_float_to_uint_cast_chain();
    std::printf("Success!\n");
    return 0;
}

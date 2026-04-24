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

void check_interval(const Expr &a, const Interval &i, bool outer) {
    Interval result =
        outer ? solve_for_outer_interval(a, "x") : solve_for_inner_interval(a, "x");
    result.min = simplify(result.min);
    result.max = simplify(result.max);
    if (!equal(result.min, i.min) || !equal(result.max, i.max)) {
        std::cerr << "Expression " << a << " solved to the interval:\n"
                  << "  min: " << result.min << "\n"
                  << "  max: " << result.max << "\n"
                  << " instead of:\n"
                  << "  min: " << i.min << "\n"
                  << "  max: " << i.max << "\n";
        std::abort();
    }
}

void check_outer_interval(const Expr &a, const Expr &min, const Expr &max) {
    check_interval(a, Interval(min, max), true);
}

void check_inner_interval(const Expr &a, const Expr &min, const Expr &max) {
    check_interval(a, Interval(min, max), false);
}

void check_and_condition(const Expr &orig, const Expr &result, const Interval &i) {
    Scope<Interval> s;
    s.push("x", i);
    Expr cond = and_condition_over_domain(orig, s);
    if (!equal(cond, result)) {
        std::cerr << "Expression " << orig
                  << " reduced to " << cond
                  << " instead of " << result << "\n";
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
// to zero the rewrite changes the expression's value even though Halide
// defines div/mod-by-zero to return zero -- `a * 0 == b` becomes `a == b/0 &&
// b%0 == 0` which collapses to `a == 0 && b == 0`, losing the original
// "always false when b != 0" semantics.
void test_nonconstant_multiplier_not_rewritten() {
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");

    // At y = 0, `x * y == 1` is the well-defined `0 == 1 == false`.
    // The buggy rewrite `x == 1/y && 1%y == 0` evaluates to
    // `x == 0 && true == true`, which is true at x = 0 -- changing the
    // value of the expression.
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
// Halide mod is defined to be non-negative (Euclidean), but the current
// float-mod lowering in CodeGen_LLVM is `a - b * floor(a/b)`, which does
// not always produce a non-negative result when b is negative -- e.g.
// fmod(5, -3) lowers to 5 - (-3)*floor(5/-3) = 5 - (-3)*(-2) = -1. The
// fuzzer exercises this via a concrete substitution and compares the
// rewritten form's simplified value against bounds_of's claim, and the
// two disagree. Until the lowering is fixed to enforce Euclidean mod,
// bounds_of falls back to unbounded for float mod. When the lowering
// is fixed, this test and the float-mod branch in Bounds.cpp can both
// go away.
void test_bounds_of_float_mod_matches_lowering() {
    Expr a = Variable::make(Float(64), "a");
    Expr b = Variable::make(Float(64), "b");
    Expr e = Mod::make(a, b);

    Scope<Interval> scope;
    scope.push("a", Interval(Expr(-10.0), Expr(-3.0)));
    scope.push("b", Interval(Expr(-9.0), Expr(4.0)));

    Interval bounds = bounds_of_expr_in_scope(e, scope);
    // Under the current lowering the result can be negative, so
    // bounds.min must not be provably non-negative.
    if (bounds.has_lower_bound()) {
        Expr provably_nonneg = simplify(bounds.min >= Expr(0.0));
        if (is_const_one(provably_nonneg)) {
            std::cerr << "bounds_of float % float claims result is non-negative, "
                      << "but the current lowering can produce negative results:\n"
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

// Regression guard for the precision side of the above fix. Carrying
// *symbolic* (non-constant) float bounds through an int32+ cast is load-
// bearing for bilateral_grid and similar pipelines: bounds often end up
// as `select(r_sigma > 0, 0/r_sigma, 1/r_sigma) + 0.5` etc., where we
// can't prove the range fits at bounds-inference time but the programmer
// has opted into the convention that float-to-int out-of-range is
// undefined. The soundness fix above must only reject *provably*
// out-of-range constant bounds, not symbolic ones.
void test_bounds_of_float_to_int_preserves_symbolic_bounds() {
    Expr rs = Variable::make(Float(32), "r_sigma");
    Expr val = Variable::make(Float(32), "val");
    Expr e = cast<int>(val / rs + 0.5f);

    Scope<Interval> scope;
    scope.push("val", Interval(Expr(0.0f), Expr(1.0f)));
    // r_sigma has a bounded positive estimate (what apply_param_estimates
    // or a scope binding effectively produces after frontend lowering).
    scope.push("r_sigma", Interval(Expr(0.1f), Expr(1.0f)));

    Interval bounds = bounds_of_expr_in_scope(e, scope);
    if (!bounds.is_bounded()) {
        std::cerr << "bounds_of int32(float_expr) dropped to unbounded "
                  << "for a case that should carry through:\n"
                  << "  expr: " << e << "\n"
                  << "  bounds.min: " << (bounds.has_lower_bound() ? simplify(bounds.min) : Expr("neg_inf")) << "\n"
                  << "  bounds.max: " << (bounds.has_upper_bound() ? simplify(bounds.max) : Expr("pos_inf")) << "\n";
        std::abort();
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

// Previously lived as `solve_test()` at the bottom of src/Solve.cpp and
// was invoked from test/internal.cpp. Moved here so all solver tests are
// in one place.
void test_original_solve_test_cases() {
    using ConciseCasts::i16;

    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    Expr z = Variable::make(Int(32), "z");

    // Check some simple cases
    check_solve(3 - 4 * x, x * (-4) + 3);
    check_solve(min(5, x), min(x, 5));
    check_solve(max(5, (5 + x) * y), max(x * y + 5 * y, 5));
    check_solve(5 * y + 3 * x == 2, ((x == ((2 - (5 * y)) / 3)) && (((2 - (5 * y)) % 3) == 0)));
    check_solve(min(min(z, x), min(x, y)), min(x, min(y, z)));
    check_solve(min(x + y, x + 5), x + min(y, 5));

    // Check solver with expressions containing division
    check_solve(x + (x * 2) / 2, x * 2);
    check_solve(x + (x * 2 + y) / 2, x * 2 + (y / 2));
    check_solve(x + (x * 2 - y) / 2, x * 2 - (y / 2));
    check_solve(x + (-(x * 2) / 2), x * 0 + 0);
    check_solve(x + (-(x * 2 + -3)) / 2, x * 0 + 1);
    check_solve(x + (z - (x * 2 + -3)) / 2, x * 0 + (z - (-3)) / 2);
    check_solve(x + (y * 16 + (z - (x * 2 + -1))) / 2,
                (x * 0) + (((z - -1) + (y * 16)) / 2));

    check_solve((x * 9 + 3) / 4 - x * 2, (x * 1 + 3) / 4);
    check_solve((x * 9 + 3) / 4 + x * 2, (x * 17 + 3) / 4);
    check_solve(x * 2 + (x * 9 + 3) / 4, (x * 17 + 3) / 4);

    // Check the solver doesn't perform transformations that change integer overflow behavior.
    check_solve(i16(x + y) * i16(2) / i16(2), i16(x + y) * i16(2) / i16(2));

    // A let statement
    check_solve(Let::make("z", 3 + 5 * x, y + z < 8),
                x <= (((8 - (3 + y)) - 1) / 5));

    // A let statement where the variable gets used twice.
    check_solve(Let::make("z", 3 + 5 * x, y + (z + z) < 8),
                x <= (((8 - (6 + y)) - 1) / 10));

    // Something where we expect a let in the output.
    {
        Expr e = y + 1;
        for (int i = 0; i < 10; i++) {
            e *= (e + 1);
        }
        SolverResult solved = solve_expression(x + e < e * e, "x");
        if (!(solved.fully_solved && solved.result.as<Let>())) {
            std::cerr << "Expected fully-solved Let-bearing result\n";
            std::abort();
        }
    }

    // Solving inequalities for integers is a pain to get right with
    // all the rounding rules. Check we didn't make a mistake with
    // brute force.
    for (int den = -3; den <= 3; den++) {
        if (den == 0) {
            continue;
        }
        for (int num = 5; num <= 10; num++) {
            Expr in[] = {
                {x * den < num},
                {x * den <= num},
                {x * den == num},
                {x * den != num},
                {x * den >= num},
                {x * den > num},
                {x / den < num},
                {x / den <= num},
                {x / den == num},
                {x / den != num},
                {x / den >= num},
                {x / den > num},
            };
            for (const auto &e : in) {
                SolverResult solved = solve_expression(e, "x");
                if (!solved.fully_solved) {
                    std::cerr << "Error: failed to solve for x in " << e << "\n";
                    std::abort();
                }
                Expr out = simplify(solved.result);
                for (int i = -10; i < 10; i++) {
                    Expr in_val = substitute("x", i, e);
                    Expr out_val = substitute("x", i, out);
                    in_val = simplify(in_val);
                    out_val = simplify(out_val);
                    if (!equal(in_val, out_val)) {
                        std::cerr << "Error: "
                                  << e << " is not equivalent to "
                                  << out << " when x == " << i << "\n";
                        std::abort();
                    }
                }
            }
        }
    }

    // Check for combinatorial explosion
    {
        Expr e = x + y;
        for (int i = 0; i < 20; i++) {
            e += (e + 1) * y;
        }
        SolverResult solved = solve_expression(e, "x");
        if (!(solved.fully_solved && solved.result.defined())) {
            std::cerr << "Expected fully-solved defined result for combinatorial case\n";
            std::abort();
        }
    }

    // Check some things that we don't expect to work.

    // Quadratics:
    if (solve_expression(x * x < 4, "x").fully_solved) {
        std::cerr << "Expected quadratic to not be fully solved\n";
        std::abort();
    }

    // Function calls, cast nodes, or multiplications by unknown sign
    // don't get inverted, but the bit containing x still gets moved
    // leftwards.
    check_solve(4.0f > sqrt(x), sqrt(x) < 4.0f);

    check_solve(4 > y * x, x * y < 4);

    // Now test solving for an interval
    check_inner_interval(x > 0, 1, Interval::pos_inf());
    check_inner_interval(x < 100, Interval::neg_inf(), 99);
    check_outer_interval(x > 0 && x < 100, 1, 99);
    check_inner_interval(x > 0 && x < 100, 1, 99);

    Expr c = Variable::make(Bool(), "c");
    check_outer_interval(Let::make("y", 0, x > y && x < 100), 1, 99);
    check_outer_interval(Let::make("c", x > 0, c && x < 100), 1, 99);

    check_outer_interval((x >= 10 && x <= 90) && sin(x) > 0.5f, 10, 90);
    check_inner_interval((x >= 10 && x <= 90) && sin(x) > 0.6f, Interval::pos_inf(), Interval::neg_inf());

    check_inner_interval(x == 10, 10, 10);
    check_outer_interval(x == 10, 10, 10);

    check_inner_interval(!(x != 10), 10, 10);
    check_outer_interval(!(x != 10), 10, 10);

    check_inner_interval(3 * x + 4 < 27, Interval::neg_inf(), 7);
    check_outer_interval(3 * x + 4 < 27, Interval::neg_inf(), 7);

    check_inner_interval(min(x, y) > 17, 18, y);
    check_outer_interval(min(x, y) > 17, 18, Interval::pos_inf());

    check_inner_interval(x / 5 < 17, Interval::neg_inf(), 84);
    check_outer_interval(x / 5 < 17, Interval::neg_inf(), 84);

    // Test anding a condition over a domain
    check_and_condition(x > 0, const_true(), Interval(1, y));
    check_and_condition(x > 0, const_true(), Interval(5, y));
    check_and_condition(x > 0, const_false(), Interval(-5, y));
    check_and_condition(x > 0 && x < 10, const_true(), Interval(1, 9));
    check_and_condition(x > 0 || sin(x) == 0.5f, const_true(), Interval(100, 200));

    check_and_condition(x <= 0, const_true(), Interval(-100, 0));
    check_and_condition(x <= 0, const_false(), Interval(-100, 1));

    check_and_condition(x <= 0 || y > 2, const_true(), Interval(-100, 0));
    check_and_condition(x > 0 || y > 2, 2 < y, Interval(-100, 0));

    check_and_condition(x == 0, const_true(), Interval(0, 0));
    check_and_condition(x == 0, const_false(), Interval(-10, 10));
    check_and_condition(x != 0, const_false(), Interval(-10, 10));
    check_and_condition(x != 0, const_true(), Interval(-20, -10));

    check_and_condition(y == 0, y == 0, Interval(-10, 10));
    check_and_condition(y != 0, y != 0, Interval(-10, 10));
    check_and_condition((x == 5) && (y != 0), const_false(), Interval(-10, 10));
    check_and_condition((x == 5) && (y != 3), y != 3, Interval(5, 5));
    check_and_condition((x != 0) && (y != 0), const_false(), Interval(-10, 10));
    check_and_condition((x != 0) && (y != 0), y != 0, Interval(-20, -10));

    {
        // This case used to break due to signed integer overflow in
        // the simplifier.
        Expr a16 = Load::make(Int(16), "a", {x}, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr b16 = Load::make(Int(16), "b", {x}, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        Expr lhs = pow(cast<int32_t>(a16), 2) + pow(cast<int32_t>(b16), 2);

        Scope<Interval> s;
        s.push("x", Interval(-10, 10));
        Expr cond = and_condition_over_domain(lhs < 0, s);
        if (is_const_one(simplify(cond))) {
            std::cerr << "Expected cond to not simplify to const_one\n";
            std::abort();
        }
    }

    {
        // This cause use to cause infinite recursion:
        Expr t = Variable::make(Int(32), "t");
        Expr test = (x <= min(max((y - min(((z * x) + t), t)), 1), 0));
        Interval result = solve_for_outer_interval(test, "z");
    }

    {
        // This case caused exponential behavior
        Expr t = Variable::make(Int(32), "t");
        for (int i = 0; i < 50; i++) {
            t = min(t, Variable::make(Int(32), unique_name('v')));
            t = max(t, Variable::make(Int(32), unique_name('v')));
        }
        solve_for_outer_interval(t <= 5, "t");
        solve_for_inner_interval(t <= 5, "t");
    }

    // Check for partial results
    check_solve(max(min(y, x), x), max(min(x, y), x));
    check_solve(min(y, x) + max(y, 2 * x), min(x, y) + max(x * 2, y));
    check_solve((min(x, y) + min(y, x)) * max(y, x), (min(x, y) * 2) * max(x, y));
    check_solve(max((min((y * x), x) + min((1 + y), x)), (y + 2 * x)),
                max((min((x * y), x) + min(x, (1 + y))), (x * 2 + y)));

    {
        Expr x = Variable::make(UInt(32), "x");
        Expr y = Variable::make(UInt(32), "y");
        Expr z = Variable::make(UInt(32), "z");
        check_solve(5 - (4 - 4 * x), x * (4) + 1);
        check_solve(z - (y - x), x + (z - y));
        check_solve(z - (y - x) == 2, x == 2 - (z - y));

        check_solve(x - (x - y), (x - x) + y);

        // This is used to cause infinite recursion
        Expr expr = Add::make(z, Sub::make(x, y));
        SolverResult solved = solve_expression(expr, "y");
    }

    // This case was incorrect due to canonicalization of the multiply
    // occurring after unpacking the LHS.
    check_solve((y - z) * x, x * (y - z));

    // These cases were incorrectly not flipping min/max when moving
    // it out of the RHS of a subtract.
    check_solve(min(x - y, x - z), x - max(y, z));
    check_solve(min(x - y, x), x - max(y, 0));
    check_solve(min(x, x - y), x - max(y, 0));
    check_solve(max(x - y, x - z), x - min(y, z));
    check_solve(max(x - y, x), x - min(y, 0));
    check_solve(max(x, x - y), x - min(y, 0));

    // Check mixed add/sub
    check_solve(min(x - y, x + z), x + min(0 - y, z));
    check_solve(max(x - y, x + z), x + max(0 - y, z));
    check_solve(min(x + y, x - z), x + min(y, 0 - z));
    check_solve(max(x + y, x - z), x + max(y, 0 - z));

    check_solve((5 * Broadcast::make(x, 4) + y) / 5,
                Broadcast::make(x, 4) + (Broadcast::make(y, 4) / 5));

    // Select negates the condition to move x leftward
    check_solve(select(y < z, z, x),
                select(z <= y, x, z));

    // Select negates the condition and then mutates it, moving x
    // leftward (despite the simplifier preferring < to >).
    check_solve(select(x < 10, 10, x),
                select(x >= 10, x, 10));
}

}  // namespace

int main(int argc, char **argv) {
    test_original_solve_test_cases();
    test_unsigned_ordering_not_rearranged();
    test_unsigned_equality_still_rearranged();
    test_nonconstant_multiplier_not_rewritten();
    test_positive_const_multiplier_still_rewritten();
    test_solve_does_not_abort_on_narrow_self_add();
    test_narrow_div_add_equivalence();
    test_bounds_of_float_mod_matches_lowering();
    test_bounds_of_float_to_int_cast_is_sound();
    test_bounds_of_float_to_int_preserves_symbolic_bounds();
    test_simplify_preserves_float_to_uint_cast_chain();
    std::printf("Success!\n");
    return 0;
}

#include "Halide.h"
#include <functional>

#include "IRGraphCXXPrinter.h"
#include "fuzz_helpers.h"
#include "random_expr_generator.h"

// Test the solver in Halide by generating random expressions and verifying that
// solve_expression, solve_for_inner_interval, solve_for_outer_interval, and
// and_condition_over_domain satisfy their respective contracts under random
// concrete substitutions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

// Wrap a call that may throw InternalError in an std::variant so callers can
// report the failure with context rather than aborting the whole fuzzer.
template<typename T>
struct SafeResult : std::variant<T, InternalError> {
    using std::variant<T, InternalError>::variant;
    bool ok() const {
        return this->index() == 0;
    }
    bool failed() const {
        return this->index() == 1;
    }
    const T &value() const {
        return std::get<T>(*this);
    }
};

SafeResult<Expr> safe_simplify(const Expr &e) {
    try {
        return simplify(e);
    } catch (InternalError &err) {
        std::cerr << "simplify threw on:\n"
                  << e << "\n"
                  << err.what() << "\n";
        return err;
    }
}

SafeResult<SolverResult> safe_solve_expression(const Expr &e, const string &var) {
    try {
        return solve_expression(e, var);
    } catch (InternalError &err) {
        std::cerr << "solve_expression threw on:\n"
                  << e << "\n  solving for \"" << var << "\"\n"
                  << err.what() << "\n";
        return err;
    }
}

SafeResult<Interval> safe_solve_for_inner_interval(const Expr &c, const string &var) {
    try {
        return solve_for_inner_interval(c, var);
    } catch (InternalError &err) {
        std::cerr << "solve_for_inner_interval threw on:\n"
                  << c << "\n  solving for \"" << var << "\"\n"
                  << err.what() << "\n";
        return err;
    }
}

SafeResult<Interval> safe_solve_for_outer_interval(const Expr &c, const string &var) {
    try {
        return solve_for_outer_interval(c, var);
    } catch (InternalError &err) {
        std::cerr << "solve_for_outer_interval threw on:\n"
                  << c << "\n  solving for \"" << var << "\"\n"
                  << err.what() << "\n";
        return err;
    }
}

SafeResult<Expr> safe_and_condition_over_domain(const Expr &c, const Scope<Interval> &scope) {
    try {
        return and_condition_over_domain(c, scope);
    } catch (InternalError &err) {
        std::cerr << "and_condition_over_domain threw on:\n"
                  << c << "\n"
                  << err.what() << "\n";
        return err;
    }
}

Expr random_int_val(FuzzingContext &fuzz, int lo, int hi) {
    return cast(Int(32), fuzz.ConsumeIntegralInRange(lo, hi));
}

// Returns true if the expression, under the given substitution, contains a
// division or modulo whose divisor simplifies to zero. Halide defines
// div/mod-by-zero to return zero, but the simplifier doesn't always fold
// that consistently across syntactically-different forms -- so solve can
// rearrange an expression into an equivalent shape whose simplified value
// at a concrete substitution differs only because one side gets the
// "returns zero" fold applied while the other doesn't. Skip those samples
// when checking equivalence. Solve often emits Let bindings, so inline
// them first (otherwise Div::b is a variable reference and we can't see
// whether it's zero).
bool has_div_or_mod_by_zero(const Expr &e, const map<string, Expr> &vars) {
    Expr inlined = substitute_in_all_lets(e);
    bool found = false;
    auto check_denom = [&](const Expr &denom) {
        if (found) return;
        if (SafeResult<Expr> r = safe_simplify(substitute(vars, denom)); r.ok()) {
            if (Internal::is_const_zero(r.value())) {
                found = true;
            }
        }
    };
    visit_with(
        inlined,
        [&](auto *self, const Div *op) {
            check_denom(op->b);
            self->visit_base(op);
        },
        [&](auto *self, const Mod *op) {
            check_denom(op->b);
            self->visit_base(op);
        });
    return found;
}

// Returns true if the expression, under the given substitution, contains a
// narrowing cast whose source value doesn't fit in the destination type.
// Halide's bounds analysis assumes such casts don't overflow (see PR #7814
// discussion) -- that's a programmer-level contract that the fuzzer's
// random value substitutions can easily violate, and the resulting
// runtime wrap then disagrees with bounds_of's "assumed-fits" prediction.
// Skip those samples when checking contracts that rely on bounds_of.
bool has_overflowing_cast(const Expr &e, const map<string, Expr> &vars) {
    Expr inlined = substitute_in_all_lets(e);
    bool found = false;
    auto check_cast = [&](const Cast *op) {
        if (found) return;
        Type to = op->type;
        Type from = op->value.type();
        // Only care about casts between integer/unsigned types that could
        // overflow the destination.
        if (!(to.is_int_or_uint() && from.is_int_or_uint())) return;
        if (to.can_represent(from)) return;
        SafeResult<Expr> r = safe_simplify(substitute(vars, op->value));
        if (!r.ok()) return;
        if (auto iv = as_const_int(r.value())) {
            if (!to.can_represent(*iv)) found = true;
        } else if (auto uv = as_const_uint(r.value())) {
            if (!to.can_represent(*uv)) found = true;
        }
    };
    visit_with(
        inlined,
        [&](auto *self, const Cast *op) {
            check_cast(op);
            self->visit_base(op);
        });
    return found;
}

// Test that solve_expression(test, var) produces an expression equivalent to
// `test` under random concrete substitutions of all variables. Modeled after
// the brute-force check at the bottom of Solve.cpp's solve_test().
bool test_solve_expression_equivalence(RandomExpressionGenerator &reg,
                                       const Expr &test,
                                       const string &var,
                                       int samples) {
    SafeResult<SolverResult> res = safe_solve_expression(test, var);
    if (res.failed()) {
        return false;
    }
    Expr solved = res.value().result;
    if (!solved.defined()) {
        std::cerr << "solve_expression returned an undefined Expr for:\n"
                  << test << "\n";
        return false;
    }

    // Solving again should not throw.
    if (safe_solve_expression(solved, var).failed()) {
        return false;
    }

    map<string, Expr> vars;
    for (const auto &v : reg.fuzz_vars) {
        vars[v.name()] = Expr();
    }

    for (int i = 0; i < samples; i++) {
        for (auto &[name, val] : vars) {
            val = random_int_val(reg.fuzz, -32, 32);
        }

        // Skip samples that invoke div/mod-by-zero in the input: Halide
        // defines the result as zero, but the simplifier may apply the
        // fold asymmetrically across two syntactically-distinct forms
        // that are otherwise semantically equivalent. We don't skip
        // based on the *solved* form -- solve must never introduce new
        // div/mod-by-zero that wasn't already in the input.
        if (has_div_or_mod_by_zero(test, vars) ||
            has_overflowing_cast(test, vars)) {
            continue;
        }

        SafeResult<Expr> test_v = safe_simplify(substitute(vars, test));
        SafeResult<Expr> solved_v = safe_simplify(substitute(vars, solved));
        if (test_v.failed() || solved_v.failed()) {
            return false;
        }

        // If either side didn't simplify to a constant, there's likely UB
        // (e.g. signed integer overflow) somewhere -- skip this sample.
        if (!Internal::is_const(test_v.value()) || !Internal::is_const(solved_v.value())) {
            continue;
        }

        if (!equal(test_v.value(), solved_v.value())) {
            std::cerr << "solve_expression produced a non-equivalent result:\n";
            for (const auto &[name, val] : vars) {
                std::cerr << "  " << name << " = " << val << "\n";
            }
            std::cerr << "  variable being solved: " << var << "\n";
            std::cerr << "  original: " << test << " -> " << test_v.value() << "\n";
            std::cerr << "  solved:   " << solved << " -> " << solved_v.value() << "\n";
            return false;
        }
    }
    return true;
}

// Substitute the given variables and simplify.
Expr subst_and_simplify(const map<string, Expr> &vars, const Expr &e) {
    return simplify(substitute(vars, e));
}

// Returns 1 if `c` simplifies to a true constant, 0 if a false constant, -1
// otherwise. Used to handle partial results from the simplifier safely.
int try_resolve_bool(const Expr &c) {
    Expr s;
    if (SafeResult<Expr> r = safe_simplify(c); r.ok()) {
        s = r.value();
    } else {
        return -1;
    }
    if (is_const_one(s)) {
        return 1;
    }
    if (is_const_zero(s)) {
        return 0;
    }
    return -1;
}

// Test the contracts of solve_for_inner_interval and solve_for_outer_interval
// by sampling values of `var` and checking:
//   - if sample is inside the inner interval, the condition must be true
//   - if sample is outside the outer interval, the condition must be false
// Non-solving variables are given concrete random values before sampling.
bool test_solve_intervals(RandomExpressionGenerator &reg,
                          const Expr &cond,
                          const string &var,
                          int samples) {
    internal_assert(cond.type().is_bool());

    SafeResult<Interval> inner_res = safe_solve_for_inner_interval(cond, var);
    SafeResult<Interval> outer_res = safe_solve_for_outer_interval(cond, var);
    if (inner_res.failed() || outer_res.failed()) {
        return false;
    }
    Interval inner = inner_res.value();
    Interval outer = outer_res.value();

    map<string, Expr> other_vars;
    for (const auto &v : reg.fuzz_vars) {
        if (v.name() != var) {
            other_vars[v.name()] = Expr();
        }
    }

    for (int i = 0; i < samples; i++) {
        for (auto &[name, val] : other_vars) {
            val = random_int_val(reg.fuzz, -16, 16);
        }
        // Skip substitutions that violate the "assumed not to overflow"
        // contract for narrowing int casts.
        if (has_overflowing_cast(cond, other_vars)) {
            continue;
        }

        Expr inner_min_v, inner_max_v, outer_min_v, outer_max_v;
        if (inner.has_lower_bound()) inner_min_v = subst_and_simplify(other_vars, inner.min);
        if (inner.has_upper_bound()) inner_max_v = subst_and_simplify(other_vars, inner.max);
        if (outer.has_lower_bound()) outer_min_v = subst_and_simplify(other_vars, outer.min);
        if (outer.has_upper_bound()) outer_max_v = subst_and_simplify(other_vars, outer.max);
        Expr cond_sub = substitute(other_vars, cond);

        int val = reg.fuzz.ConsumeIntegralInRange(-64, 64);
        Expr var_val = cast(Int(32), val);
        int cond_truth = try_resolve_bool(substitute(var, var_val, cond_sub));
        if (cond_truth < 0) {
            // Can't resolve (symbolic leftover or UB) -- skip.
            continue;
        }

        // Inner interval: var_val in [inner.min, inner.max] => cond is true.
        // An empty inner interval is a trivial (vacuously true) claim.
        int in_inner = inner.is_empty() ? 0 : 1;
        if (in_inner == 1 && inner.has_lower_bound()) {
            int r = try_resolve_bool(var_val >= inner_min_v);
            if (r < 0) {
                in_inner = -1;
            } else if (r == 0) {
                in_inner = 0;
            }
        }
        if (in_inner == 1 && inner.has_upper_bound()) {
            int r = try_resolve_bool(var_val <= inner_max_v);
            if (r < 0) {
                in_inner = -1;
            } else if (r == 0) {
                in_inner = 0;
            }
        }
        if (in_inner == 1 && cond_truth == 0) {
            std::cerr << "solve_for_inner_interval violation\n"
                      << "  cond: " << cond << "\n"
                      << "  var: " << var << " = " << val << "\n"
                      << "  inner interval: [" << inner.min << ", " << inner.max << "]\n";
            for (const auto &[name, v] : other_vars) {
                std::cerr << "  " << name << " = " << v << "\n";
            }
            return false;
        }

        // Outer interval: var_val NOT in [outer.min, outer.max] => cond is false.
        // An empty outer interval means cond is unsatisfiable, so any sample
        // that evaluates to true is a violation.
        int out_lb = 0, out_ub = 0;
        if (outer.is_empty()) {
            out_lb = 1;
        }
        if (outer.has_lower_bound()) {
            int r = try_resolve_bool(var_val < outer_min_v);
            if (r < 0) {
                out_lb = -1;
            } else {
                out_lb = r;
            }
        }
        if (outer.has_upper_bound()) {
            int r = try_resolve_bool(var_val > outer_max_v);
            if (r < 0) {
                out_ub = -1;
            } else {
                out_ub = r;
            }
        }
        if ((out_lb == 1 || out_ub == 1) && cond_truth == 1) {
            std::cerr << "solve_for_outer_interval violation\n"
                      << "  cond: " << cond << "\n"
                      << "  var: " << var << " = " << val << "\n"
                      << "  outer interval: [" << outer.min << ", " << outer.max << "]\n";
            for (const auto &[name, v] : other_vars) {
                std::cerr << "  " << name << " = " << v << "\n";
            }
            return false;
        }
    }
    return true;
}

// Test that and_condition_over_domain(c, scope) implies c on the domain:
// for any concrete assignment of vars within their scope intervals, if the
// weakened condition is true, the original condition must also be true.
bool test_and_condition_over_domain(RandomExpressionGenerator &reg,
                                    const Expr &cond,
                                    int samples) {
    internal_assert(cond.type().is_bool());

    Scope<Interval> scope;
    map<string, std::pair<int, int>> ranges;
    for (const auto &v : reg.fuzz_vars) {
        int a = reg.fuzz.ConsumeIntegralInRange(-16, 16);
        int b = reg.fuzz.ConsumeIntegralInRange(-16, 16);
        if (a > b) std::swap(a, b);
        ranges[v.name()] = {a, b};
        scope.push(v.name(), Interval(cast(Int(32), a), cast(Int(32), b)));
    }

    SafeResult<Expr> weakened_res = safe_and_condition_over_domain(cond, scope);
    if (weakened_res.failed()) {
        return false;
    }
    Expr weakened = weakened_res.value();

    for (int i = 0; i < samples; i++) {
        map<string, Expr> vars;
        for (const auto &[name, r] : ranges) {
            vars[name] = random_int_val(reg.fuzz, r.first, r.second);
        }
        // Skip substitutions that violate the "assumed not to overflow"
        // contract for narrowing int casts (see has_overflowing_cast).
        if (has_overflowing_cast(cond, vars)) {
            continue;
        }
        int cond_truth = try_resolve_bool(substitute(vars, cond));
        int weak_truth = try_resolve_bool(substitute(vars, weakened));
        if (cond_truth < 0 || weak_truth < 0) {
            continue;
        }
        if (weak_truth == 1 && cond_truth == 0) {
            std::cerr << "and_condition_over_domain violation (result does not imply input):\n"
                      << "  cond:     " << cond << "\n"
                      << "  weakened: " << weakened << "\n";
            for (const auto &[n, v] : vars) {
                std::cerr << "  " << n << " = " << v << " (in [" << ranges[n].first
                          << ", " << ranges[n].second << "])\n";
            }
            return false;
        }
    }
    return true;
}

Expr random_comparison(RandomExpressionGenerator &reg, int depth) {
    using make_bin_op_fn = Expr (*)(Expr, Expr);
    static make_bin_op_fn ops[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };
    Expr a = reg.random_expr(Int(32), depth);
    Expr b = reg.random_expr(Int(32), depth);
    return reg.fuzz.PickValueInArray(ops)(a, b);
}

}  // namespace

FUZZ_TEST(solve, FuzzingContext &fuzz) {
    // Depth of the randomly generated expression trees.
    constexpr int depth = 6;
    // Number of samples to test each invariant at.
    constexpr int samples = 20;

    RandomExpressionGenerator reg{fuzz};
    reg.fuzz_types = {Int(8), Int(16), Int(32), Int(64),
                      UInt(1), UInt(8), UInt(16), UInt(32), UInt(64),
                      Float(32), Float(64)};
    // Leave gen_shuffles / gen_vector_reduce / gen_reinterpret off for now
    // -- those exercise Deinterleaver / shuffle lowering more than solve
    // proper. gen_broadcast_of_vector and gen_ramp_of_vector are on so the
    // solver sees vector-typed expressions.
    reg.gen_shuffles = false;
    reg.gen_vector_reduce = false;
    reg.gen_reinterpret = false;

    // Pick one of the generator's variables to solve for.
    const string var = reg.fuzz_vars[fuzz.ConsumeIntegralInRange<size_t>(0, reg.fuzz_vars.size() - 1)].name();

    // solve_expression: arithmetic equivalence. Pick a random width so the
    // generator's Broadcast/Ramp lambdas actually fire (they're no-ops on
    // scalar types). Vector subtrees containing scalar variables exercise
    // the solver's vector-aware handling (see e.g. the Broadcast case in
    // src/Solve.cpp's solve_test).
    int width = fuzz.PickValueInArray({1, 2, 3, 4, 6, 8});
    Expr test_expr = reg.random_expr(Int(32).with_lanes(width), depth);
    if (!test_solve_expression_equivalence(reg, test_expr, var, samples)) {
        std::cerr << "Failing expression (C++):\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(test_expr);
        std::cerr << "Expr final_expr = " << printer.node_names[test_expr.get()] << ";\n";
        std::cerr << "  solving for \"" << var << "\"\n";
        return 1;
    }

    // solve_expression: also handle comparisons (the solver inverts these).
    Expr cmp = random_comparison(reg, depth);
    if (!test_solve_expression_equivalence(reg, cmp, var, samples)) {
        std::cerr << "Failing comparison (C++):\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(cmp);
        std::cerr << "Expr final_expr = " << printer.node_names[cmp.get()] << ";\n";
        std::cerr << "  solving for \"" << var << "\"\n";
        return 1;
    }

    // solve_for_inner_interval / solve_for_outer_interval.
    if (!test_solve_intervals(reg, cmp, var, samples)) {
        std::cerr << "Failing condition (C++):\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(cmp);
        std::cerr << "Expr final_expr = " << printer.node_names[cmp.get()] << ";\n";
        std::cerr << "  solving for \"" << var << "\"\n";
        return 1;
    }

    // Also exercise solve_for_*_interval with compound boolean conditions.
    Expr cmp2 = random_comparison(reg, depth);
    Expr compound = fuzz.ConsumeBool() ? (cmp && cmp2) : (cmp || cmp2);
    if (!test_solve_intervals(reg, compound, var, samples)) {
        std::cerr << "Failing compound condition (C++):\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(compound);
        std::cerr << "Expr final_expr = " << printer.node_names[compound.get()] << ";\n";
        std::cerr << "  solving for \"" << var << "\"\n";
        return 1;
    }

    // and_condition_over_domain.
    if (!test_and_condition_over_domain(reg, compound, samples)) {
        std::cerr << "Failing condition (C++):\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(compound);
        std::cerr << "Expr final_expr = " << printer.node_names[compound.get()] << ";\n";
        return 1;
    }

    return 0;
}

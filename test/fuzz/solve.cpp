#include "Halide.h"
#include <functional>

#include "IRGraphCXXPrinter.h"
#include "fuzz_helpers.h"
#include "random_expr_generator.h"

// Test the solver in Halide by generating random expressions and verifying that
// solve_expression, solve_for_inner_interval, and solve_for_outer_interval
// satisfy their respective contracts under random concrete substitutions.
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

Expr random_int_val(FuzzingContext &fuzz, int lo, int hi) {
    return cast(Int(32), fuzz.ConsumeIntegralInRange(lo, hi));
}
// Returns true if the expression, under the given substitution, contains a
// floating-point division or modulo whose divisor simplifies to zero. Integer
// div/mod-by-zero is defined in Halide, but floating-point div/mod-by-zero is
// undefined and can introduce NaNs, violating the simplifier's fast-math
// assumptions.
bool has_float_div_or_mod_by_zero(const Expr &e, const map<string, Expr> &vars) {
    Expr inlined = substitute_in_all_lets(e);
    bool found = false;
    visit_with(
        inlined,
        [&](auto *self, const auto *op) {
            if constexpr (std::is_same_v<decltype(op), const Div *> ||
                          std::is_same_v<decltype(op), const Mod *>) {
                if (found || !op->type.is_float()) {
                    return;
                }
                SafeResult<Expr> r = safe_simplify(substitute(vars, op->b));
                found |= r.ok() && is_const_zero(r.value());
            }
            self->visit_base(op);
        });
    return found;
}

bool no_overflow_int(Type t) {
    return t.is_int() && t.bits() >= 32;
}

// Returns true if the expression, under the given substitution, contains a
// narrowing cast whose source value doesn't fit in the destination type.
// Halide assumes casts to no-overflow integer types don't overflow; skip
// substitutions that violate that programmer-level contract.
bool has_overflowing_cast(const Expr &e, const map<string, Expr> &vars) {
    Expr inlined = substitute_in_all_lets(e);
    bool found = false;
    auto check_cast = [&](const Cast *op) {
        if (found) {
            return;
        }
        Type to = op->type;
        Type from = op->value.type();
        if (!no_overflow_int(to) || !from.is_int_or_uint() || to.can_represent(from)) {
            return;
        }
        SafeResult<Expr> r = safe_simplify(substitute(vars, op->value));
        if (!r.ok()) {
            return;
        }
        if (auto iv = as_const_int(r.value()); iv && !to.can_represent(*iv)) {
            found = true;
        }
        if (auto uv = as_const_uint(r.value()); uv && !to.can_represent(*uv)) {
            found = true;
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

        // Skip substitutions that violate Halide's no-overflow cast contract
        // or its no-NaN/finite floating-point assumptions.
        if (has_float_div_or_mod_by_zero(test, vars) ||
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
std::optional<bool> try_resolve_bool(const Expr &c) {
    if (const SafeResult<Expr> r = safe_simplify(c); r.ok()) {
        if (is_const_one(r.value())) {
            return true;
        }
        if (is_const_zero(r.value())) {
            return false;
        }
    }
    return std::nullopt;
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
        // contract for casts to no-overflow integer types.
        if (has_overflowing_cast(cond, other_vars) ||
            has_float_div_or_mod_by_zero(cond, other_vars)) {
            continue;
        }

        Expr inner_min_v = inner.has_lower_bound() ? subst_and_simplify(other_vars, inner.min) : Expr();
        Expr inner_max_v = inner.has_upper_bound() ? subst_and_simplify(other_vars, inner.max) : Expr();
        Expr outer_min_v = outer.has_lower_bound() ? subst_and_simplify(other_vars, outer.min) : Expr();
        Expr outer_max_v = outer.has_upper_bound() ? subst_and_simplify(other_vars, outer.max) : Expr();
        Expr cond_sub = substitute(other_vars, cond);

        int val = reg.fuzz.ConsumeIntegralInRange(-64, 64);
        Expr var_val = cast(Int(32), val);
        map<string, Expr> vars = other_vars;
        vars[var] = var_val;
        if (has_float_div_or_mod_by_zero(cond, vars)) {
            continue;
        }
        auto cond_truth = try_resolve_bool(substitute(var, var_val, cond_sub));
        if (!cond_truth.has_value()) {
            // Can't resolve (symbolic leftover or UB) -- skip.
            continue;
        }

        // Inner interval: var_val in [inner.min, inner.max] => cond is true.
        // An empty inner interval is a trivial (vacuously true) claim.
        std::optional in_inner = !inner.is_empty();
        if (in_inner.value_or(false) && inner.has_lower_bound()) {
            in_inner = try_resolve_bool(var_val >= inner_min_v);
        }
        if (in_inner.value_or(false) && inner.has_upper_bound()) {
            in_inner = try_resolve_bool(var_val <= inner_max_v);
        }
        if (in_inner.value_or(false) && !*cond_truth) {
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
        auto out_lb = outer.has_lower_bound() ? try_resolve_bool(var_val < outer_min_v) : outer.is_empty();
        auto out_ub = outer.has_upper_bound() ? try_resolve_bool(var_val > outer_max_v) : false;
        if ((out_lb.value_or(false) || out_ub.value_or(false)) && *cond_truth) {
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

    return 0;
}

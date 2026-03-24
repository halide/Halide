#include "Halide.h"
#include <functional>

#include "ExprInterpreter.h"
#include "fuzz_helpers.h"
#include "random_expr_generator.h"

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

bool test_simplification(const Expr &a, const Expr &b, const map<string, Expr> &vars) {
    if (equal(a, b) && !a.same_as(b)) {
        std::cerr << "Simplifier created new IR node but made no changes:\n"
                  << a << "\n";
        return false;
    }
    if (Expr sb = simplify(b); !equal(b, sb)) {
        // Test all sub-expressions in pre-order traversal to minimize
        bool found_failure = false;
        mutate_with(a, [&](auto *self, const Expr &e) {
            self->mutate_base(e);
            Expr s = simplify(e);
            Expr ss = simplify(s);
            if (!found_failure && !equal(s, ss)) {
                std::cerr << "Idempotency failure\n    "
                          << e << "\n -> "
                          << s << "\n -> "
                          << ss << "\n";
                // These are broken out below to make it easier to parse any logging
                // added to the simplifier to debug the failure.
                std::cerr << "---------------------------------\n"
                          << "Begin simplification of original:\n"
                          << simplify(e) << "\n";
                std::cerr << "---------------------------------\n"
                          << "Begin resimplification of result:\n"
                          << simplify(s) << "\n"
                          << "---------------------------------\n";

                found_failure = true;
            }
            return e;
        });
        return false;
    }

    Expr a_v = simplify(substitute(vars, a));
    Expr b_v = simplify(substitute(vars, b));
    // If the simplifier didn't produce constants, there must be
    // undefined behavior in this expression. Ignore it.
    if (!Internal::is_const(a_v) || !Internal::is_const(b_v)) {
        return true;
    }
    if (!equal(a_v, b_v)) {
        std::cerr << "Simplified Expr is not equal() to Original Expr!\n";

        for (const auto &[var, val] : vars) {
            std::cerr << "Var " << var << " = " << val << "\n";
        }

        std::cerr << "Original Expr is: " << a << "\n";
        std::cerr << "Simplified Expr is: " << b << "\n";
        std::cerr << "   " << a << " -> " << a_v << "\n";
        std::cerr << "   " << b << " -> " << b_v << "\n";
        return false;
    }

    return true;
}

bool test_expression(RandomExpressionGenerator &reg, const Expr &test, int samples) {
    Expr simplified = simplify(test);

    map<string, Expr> vars;
    for (const auto &fuzz_var : reg.fuzz_vars) {
        vars[fuzz_var.name()] = Expr();
    }

    for (int i = 0; i < samples; i++) {
        for (auto &[var, val] : vars) {
            constexpr size_t kMaxLeafIterations = 10000;
            // Don't let the random leaf depend on v itself.
            size_t iterations = 0;
            do {
                val = reg.random_leaf(Int(32), true);
                iterations++;
            } while (expr_uses_var(val, var) && iterations < kMaxLeafIterations);
        }

        if (!test_simplification(test, simplified, vars)) {
            return false;
        }
    }

    // Additionally test a few rounds with the ExprInterpreter to test
    // if the simplification was correct.
    for (int i = 0; i < samples; ++i) {
        ExprInterpreter ei;
        for (const auto &fuzz_var : reg.fuzz_vars) {
            ExprInterpreter::EvalValue val(Int(32));
            val.lanes[0] = reg.fuzz.ConsumeIntegral<int32_t>();
            ei.var_env[fuzz_var.name()] = std::move(val);
        }
        ExprInterpreter::EvalValue eval_test = ei.eval(test);
        ExprInterpreter::EvalValue eval_simplified = ei.eval(simplified);
        if (eval_test.did_overflow || eval_simplified.did_overflow) {
            // The expression interpreter detected overflow on types that are
            // defined by halide to be not-overflowable. So the simplifier will
            // have done transformations which don't hold when the numbers do overflow.
            continue;  // Try different numbers instead!
        }
        bool good = true;
        if (eval_test.type != eval_simplified.type) {
            good = false;
        } else {
            if (eval_test.type.is_float()) {
                if (!eval_test.is_close(eval_simplified, 1e-5)) {
                    good = false;
                }
            } else {
                if (eval_test != eval_simplified) {
                    good = false;
                }
            }
        }
        if (!good) {
            std::cerr << "ExprInterpreter of the following Exprs did not match:\n\n";
            std::cerr << "Original: " << test << "\n";
            std::cerr << "Value: " << eval_test << "\n\n";
            std::cerr << "Simplified: " << simplified << "\n";
            std::cerr << "Value: " << eval_simplified << "\n\n";
            std::cerr << "With the following variables values:\n";
            for (const auto &var : ei.var_env) {
                std::cerr << "\t" << var.first << " = " << var.second << "\n";
            }

            return false;
        }
    }

    return true;
}

Expr simplify_at_depth(int limit, const Expr &in) {
    return mutate_with(in, [&](auto *self, const Expr &e) {
        if (limit == 0) {
            return simplify(e);
        }
        limit--;
        Expr new_e = self->mutate_base(e);
        limit++;
        return new_e;
    });
}

}  // namespace

FUZZ_TEST(simplify, FuzzingContext &fuzz) {
    // Depth of the randomly generated expression trees.
    constexpr int depth = 6;
    // Number of samples to test the generated expressions for.
    constexpr int samples = 3;
    // Number of samples to test the generated expressions for during minimization.
    constexpr int samples_during_minimization = 100;

    RandomExpressionGenerator reg{fuzz};
    // FIXME: UInt64 fails!
    reg.fuzz_types = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};
    // FIXME: These need to be disabled (otherwise crashes and/or failures):
    // reg.gen_ramp_of_vector = false;
    // reg.gen_broadcast_of_vector = false;
    reg.gen_vector_reduce = false;
    reg.gen_reinterpret = false;
    reg.gen_shuffles = false;

    int width = fuzz.PickValueInArray({1, 2, 3, 4, 6, 8});
    Expr test = reg.random_expr(reg.random_type(width), depth);
    debug(1) << "Testing " << test << "\n";

    if (!test_expression(reg, test, samples)) {
        // Failure. Find the minimal subexpression that failed.
        std::cerr << "Testing subexpressions...\n";
        bool found_failure = false;
        test = mutate_with(test, [&](auto *self, const Expr &e) {
            self->mutate_base(e);
            if (e.type().bits() && !found_failure) {
                for (int i = 1; i < 4 && !found_failure; i++) {
                    Expr limited = simplify_at_depth(i, e);
                    found_failure = !test_expression(reg, limited, samples_during_minimization);
                    if (found_failure) {
                        return limited;
                    }
                }
                if (!found_failure) {
                    found_failure = !test_expression(reg, e, samples_during_minimization);
                }
            }
            return e;
        });
        std::cerr << "Final test case: " << test << "\n";
        return 1;
    }

    return 0;
}

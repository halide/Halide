#include "Halide.h"
#include <functional>

#include "IRGraphCXXPrinter.h"
#include "fuzz_helpers.h"
#include "random_expr_generator.h"

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

bool test_simplification(Expr a, Expr b, const map<string, Expr> &vars) {
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

bool test_expression(RandomExpressionGenerator &reg, Expr test, int samples) {
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

        std::cerr << "\n\nC++ code:\n\n";
        IRGraphCXXPrinter printer(std::cerr);
        printer.print(test);
        std::cerr << "Expr final_expr = " << printer.node_names[test.get()] << ";\n";
        std::cerr << "\n\n";

        return 1;
    }

    return 0;
}

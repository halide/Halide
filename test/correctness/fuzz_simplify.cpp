#include "Halide.h"
#include <array>
#include <functional>
#include <random>

#include "random_expr_generator.h"

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Fuzz;

bool test_simplification(Expr a, Expr b, Type t, const map<string, Expr> &vars) {
    if (equal(a, b) && !a.same_as(b)) {
        std::cerr << "Simplifier created new IR node but made no changes:\n"
                  << a << "\n";
        return false;
    }
    if (Expr sb = simplify(b); !equal(b, sb)) {
        std::cerr << "Idempotency failure!\n    " << a << "\n -> " << b << "\n -> " << sb << "\n";
        // These are broken out below to make it easier to parse any logging
        // added to the simplifier to debug the failure.
        std::cerr << "---------------------------------\n"
                  << "Begin simplification of original:\n"
                  << simplify(a) << "\n";
        std::cerr << "---------------------------------\n"
                  << "Begin resimplification of result:\n"
                  << simplify(b) << "\n"
                  << "---------------------------------\n";

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

bool test_expression(RandomEngine &rng, Expr test, int samples) {
    Expr simplified = simplify(test);

    map<string, Expr> vars;
    for (int i = 0; i < fuzz_var_count; i++) {
        vars[fuzz_var(i)] = Expr();
    }

    for (int i = 0; i < samples; i++) {
        for (auto &[var, val] : vars) {
            constexpr size_t kMaxLeafIterations = 10000;
            // Don't let the random leaf depend on v itself.
            size_t iterations = 0;
            do {
                val = random_leaf(rng, Int(32), true);
                iterations++;
            } while (expr_uses_var(val, var) && iterations < kMaxLeafIterations);
        }

        if (!test_simplification(test, simplified, test.type(), vars)) {
            return false;
        }
    }
    return true;
}

template<typename T>
T initialize_rng() {
    constexpr size_t kStateWords = T::state_size * sizeof(typename T::result_type) / sizeof(uint32_t);
    std::vector<uint32_t> random(kStateWords);
    std::generate(random.begin(), random.end(), std::random_device{});
    std::seed_seq seed_seq(random.begin(), random.end());
    return T{seed_seq};
}

}  // namespace

int main(int argc, char **argv) {
    // Depth of the randomly generated expression trees.
    constexpr int depth = 6;
    // Number of samples to test the generated expressions for.
    constexpr int samples = 3;

    auto seed_generator = initialize_rng<RandomEngine>();

    for (int i = 0; i < ((argc == 1) ? 10000 : 1); i++) {
        auto seed = seed_generator();
        if (argc > 1) {
            std::istringstream{argv[1]} >> seed;
        }
        // Print the seed on every iteration so that if the simplifier crashes
        // (rather than the check failing), we can reproduce.
        std::cout << "Seed: " << seed << "\n";
        RandomEngine rng{seed};
        std::array<int, 6> vector_widths = {1, 2, 3, 4, 6, 8};
        int width = random_choice(rng, vector_widths);
        Type VT = random_type(rng, width);
        // Generate a random expr...
        Expr test = random_expr(rng, VT, depth);
        if (!test_expression(rng, test, samples)) {

            class LimitDepth : public IRMutator {
                int limit;

            public:
                using IRMutator::mutate;

                Expr mutate(const Expr &e) override {
                    if (limit == 0) {
                        return simplify(e);
                    } else {
                        limit--;
                        Expr new_e = IRMutator::mutate(e);
                        limit++;
                        return new_e;
                    }
                }

                LimitDepth(int l)
                    : limit(l) {
                }
            };

            // Failure. Find the minimal subexpression that failed.
            std::cout << "Testing subexpressions...\n";
            class TestSubexpressions : public IRMutator {
                RandomEngine &rng;
                bool found_failure = false;

            public:
                using IRMutator::mutate;
                Expr mutate(const Expr &e) override {
                    // We know there's a failure here somewhere, so test
                    // subexpressions more aggressively.
                    constexpr int samples = 100;
                    IRMutator::mutate(e);
                    if (e.type().bits() && !found_failure) {
                        Expr limited;
                        for (int i = 1; i < 4 && !found_failure; i++) {
                            limited = LimitDepth(i).mutate(e);
                            found_failure = !test_expression(rng, limited, samples);
                        }
                        if (!found_failure) {
                            found_failure = !test_expression(rng, e, samples);
                        }
                    }
                    return e;
                }

                TestSubexpressions(RandomEngine &rng)
                    : rng(rng) {
                }
            } tester(rng);
            tester.mutate(test);

            std::cout << "Failed with seed " << seed << "\n";
            return 1;
        }
    }

    std::cout << "Success!\n";
    return 0;
}

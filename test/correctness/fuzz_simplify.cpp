#include "Halide.h"
#include <array>
#include <functional>
#include <random>
#include <stdio.h>
#include <time.h>

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

typedef Expr (*make_bin_op_fn)(Expr, Expr);

const int fuzz_var_count = 5;

Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

Expr random_var(std::mt19937 &rng, Type t) {
    int fuzz_count = rng() % (fuzz_var_count - 1);
    return cast(t, Variable::make(Int(32), fuzz_var(fuzz_count)));
}

template<typename T, int N>
T random_choice(std::mt19937 &rng, const T (&choices)[N]) {
    return choices[rng() % N];
}

template<typename T>
T random_choice(std::mt19937 &rng, const std::vector<T> &choices) {
    return choices[rng() % choices.size()];
}

template<typename T, size_t N>
T random_choice(std::mt19937 &rng, const std::array<T, N> &choices) {
    return choices[rng() % N];
}

Type random_type(std::mt19937 &rng, int width) {
    Type t = random_choice(rng, fuzz_types);
    if (width > 1) {
        t = t.with_lanes(width);
    }
    return t;
}

int get_random_divisor(std::mt19937 &rng, Type t) {
    std::vector<int> divisors = {t.lanes()};
    for (int dd = 2; dd < t.lanes(); dd++) {
        if (t.lanes() % dd == 0) {
            divisors.push_back(dd);
        }
    }

    return random_choice(rng, divisors);
}

Expr random_leaf(std::mt19937 &rng, Type t, bool overflow_undef = false, bool imm_only = false) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }
    if (t.is_scalar()) {
        if (!imm_only && (rng() & 1)) {
            return random_var(rng, t);
        } else {
            if (overflow_undef) {
                // For Int(32), we don't care about correctness during
                // overflow, so just use numbers that are unlikely to
                // overflow.
                return cast(t, (int32_t)((int8_t)(rng() & 255)));
            } else {
                return cast(t, (int32_t)(rng()));
            }
        }
    } else {
        int lanes = get_random_divisor(rng, t);
        if (rng() & 1) {
            auto e1 = random_leaf(rng, t.with_lanes(t.lanes() / lanes), overflow_undef);
            auto e2 = random_leaf(rng, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Ramp::make(e1, e2, lanes);
        } else {
            auto e1 = random_leaf(rng, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Broadcast::make(e1, lanes);
        }
    }
}

Expr random_expr(std::mt19937 &rng, Type t, int depth, bool overflow_undef = false);

Expr random_condition(std::mt19937 &rng, Type t, int depth, bool maybe_scalar) {
    static make_bin_op_fn make_bin_op[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };

    if (maybe_scalar && (rng() & 1)) {
        t = t.element_of();
    }

    Expr a = random_expr(rng, t, depth);
    Expr b = random_expr(rng, t, depth);
    return random_choice(rng, make_bin_op)(a, b);
}

Expr make_absd(Expr a, Expr b) {
    // random_expr() assumes that the result t is the same as the input t,
    // which isn't true for all absd variants, so force the issue.
    return cast(a.type(), absd(a, b));
}

Expr random_expr(std::mt19937 &rng, Type t, int depth, bool overflow_undef) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }

    if (depth-- <= 0) {
        return random_leaf(rng, t, overflow_undef);
    }

    std::function<Expr()> operations[] = {
        [&]() {
            return random_leaf(rng, t);
        },
        [&]() {
            auto c = random_condition(rng, t, depth, true);
            auto e1 = random_expr(rng, t, depth, overflow_undef);
            auto e2 = random_expr(rng, t, depth, overflow_undef);
            return select(c, e1, e2);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(rng, t);
                auto e1 = random_expr(rng, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Broadcast::make(e1, lanes);
            }
            return random_expr(rng, t, depth, overflow_undef);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(rng, t);
                auto e1 = random_expr(rng, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                auto e2 = random_expr(rng, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Ramp::make(e1, e2, lanes);
            }
            return random_expr(rng, t, depth, overflow_undef);
        },
        [&]() {
            if (t.is_bool()) {
                auto e1 = random_expr(rng, t, depth);
                return Not::make(e1);
            }
            return random_expr(rng, t, depth, overflow_undef);
        },
        [&]() {
            // When generating boolean expressions, maybe throw in a condition on non-bool types.
            if (t.is_bool()) {
                return random_condition(rng, random_type(rng, t.lanes()), depth, false);
            }
            return random_expr(rng, t, depth, overflow_undef);
        },
        [&]() {
            // Get a random type that isn't t or int32 (int32 can overflow and we don't care about that).
            // Note also that the std::mt19937 doesn't actually promise to return a random distribution --
            // it can (e.g.) decide to just return 0 for all data, forever -- so this loop has no guarantee
            // of eventually finding a different type. To remedy this, we'll just put a limit on the retries.
            int count = 0;
            Type subtype;
            do {
                subtype = random_type(rng, t.lanes());
            } while (++count < 10 && (subtype == t || (subtype.is_int() && subtype.bits() == 32)));
            auto e1 = random_expr(rng, subtype, depth, overflow_undef);
            return Cast::make(t, e1);
        },
        [&]() {
            static make_bin_op_fn make_bin_op[] = {
                // Arithmetic operations.
                Add::make,
                Sub::make,
                Mul::make,
                Min::make,
                Max::make,
                Div::make,
                Mod::make,
                make_absd,
            };

            Expr a = random_expr(rng, t, depth, overflow_undef);
            Expr b = random_expr(rng, t, depth, overflow_undef);
            return random_choice(rng, make_bin_op)(a, b);
        },
        [&]() {
            static make_bin_op_fn make_bin_op[] = {
                And::make,
                Or::make,
            };

            // Boolean operations -- both sides must be cast to booleans,
            // and then we must cast the result back to 't'.
            Expr a = random_expr(rng, t, depth, overflow_undef);
            Expr b = random_expr(rng, t, depth, overflow_undef);
            Type bool_with_lanes = Bool(t.lanes());
            a = cast(bool_with_lanes, a);
            b = cast(bool_with_lanes, b);
            return cast(t, random_choice(rng, make_bin_op)(a, b));
        }};
    return random_choice(rng, operations)();
}

bool test_simplification(Expr a, Expr b, Type t, const map<string, Expr> &vars) {
    for (int j = 0; j < t.lanes(); j++) {
        Expr a_j = a;
        Expr b_j = b;
        if (t.lanes() != 1) {
            a_j = extract_lane(a, j);
            b_j = extract_lane(b, j);
        }

        Expr a_j_v = simplify(substitute(vars, a_j));
        Expr b_j_v = simplify(substitute(vars, b_j));
        // If the simplifier didn't produce constants, there must be
        // undefined behavior in this expression. Ignore it.
        if (!Internal::is_const(a_j_v) || !Internal::is_const(b_j_v)) {
            continue;
        }
        if (!equal(a_j_v, b_j_v)) {
            std::cerr << "Simplified Expr is not equal() to Original Expr!\n";

            for (map<string, Expr>::const_iterator i = vars.begin(); i != vars.end(); i++) {
                std::cerr << "Var " << i->first << " = " << i->second << "\n";
            }

            std::cerr << "Original Expr is: " << a << "\n";
            std::cerr << "Simplified Expr is: " << b << "\n";
            std::cerr << "In vector lane " << j << ", original -> simplified:\n";
            std::cerr << "   " << a_j << " -> " << a_j_v << "\n";
            std::cerr << "   " << b_j << " -> " << b_j_v << "\n";
            return false;
        }
    }
    return true;
}

bool test_expression(std::mt19937 &rng, Expr test, int samples) {
    Expr simplified = simplify(test);

    map<string, Expr> vars;
    for (int i = 0; i < fuzz_var_count; i++) {
        vars[fuzz_var(i)] = Expr();
    }

    for (int i = 0; i < samples; i++) {
        for (std::map<string, Expr>::iterator v = vars.begin(); v != vars.end(); v++) {
            size_t kMaxLeafIterations = 10000;
            // Don't let the random leaf depend on v itself.
            size_t iterations = 0;
            do {
                v->second = random_leaf(rng, Int(32), true);
                iterations++;
            } while (expr_uses_var(v->second, v->first) && iterations < kMaxLeafIterations);
        }

        if (!test_simplification(test, simplified, test.type(), vars)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    // Depth of the randomly generated expression trees.
    const int depth = 5;
    // Number of samples to test the generated expressions for.
    const int samples = 3;

    std::mt19937 seed_generator{(uint32_t)time(NULL)};

    for (int i = 0; i < ((argc == 1) ? 10000 : 1); i++) {
        uint32_t seed = seed_generator();
        if (argc > 1) {
            seed = atoi(argv[1]);
        }
        // Print the seed on every iteration so that if the simplifier crashes
        // (rather than the check failing), we can reproduce.
        printf("Seed: %d\n", seed);
        std::mt19937 rng{seed};
        std::array<int, 6> vector_widths = {1, 2, 3, 4, 6, 8};
        int width = random_choice(rng, vector_widths);
        Type VT = random_type(rng, width);
        // Generate a random expr...
        Expr test = random_expr(rng, VT, depth);
        if (!test_expression(rng, test, samples)) {

            // Failure. Find the minimal subexpression that failed.
            printf("Testing subexpressions...\n");
            class TestSubexpressions : public IRMutator {
                std::mt19937 &rng;
                bool found_failure = false;

            public:
                using IRMutator::mutate;
                Expr mutate(const Expr &e) override {
                    // We know there's a failure here somewhere, so test
                    // subexpressions more aggressively.
                    IRMutator::mutate(e);
                    if (e.type().bits() && !found_failure) {
                        const int samples = 100;
                        found_failure = !test_expression(rng, e, samples);
                    }
                    return e;
                }

                TestSubexpressions(std::mt19937 &rng)
                    : rng(rng) {
                }
            } tester(rng);
            tester.mutate(test);

            printf("Failed with seed %d\n", seed);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}

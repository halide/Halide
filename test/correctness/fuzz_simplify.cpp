#include "Halide.h"
#include <array>
#include <functional>
#include <random>

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.
namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

typedef Expr (*make_bin_op_fn)(Expr, Expr);

constexpr int fuzz_var_count = 5;

Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

Expr random_var(std::mt19937 &rng, Type t) {
    std::uniform_int_distribution dist(0, fuzz_var_count - 1);
    int fuzz_count = dist(rng);
    return cast(t, Variable::make(Int(32), fuzz_var(fuzz_count)));
}

template<typename T>
decltype(auto) random_choice(std::mt19937 &rng, T &&choices) {
    std::uniform_int_distribution<size_t> dist(0, std::size(choices) - 1);
    return choices[dist(rng)];
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

Expr make_bitwise_or(Expr a, Expr b) {
    return a | b;
}

Expr make_bitwise_and(Expr a, Expr b) {
    return a & b;
}

Expr make_bitwise_xor(Expr a, Expr b) {
    return a ^ b;
}

Expr make_abs(Expr a, Expr) {
    if (!a.type().is_uint()) {
        return cast(a.type(), abs(a));
    } else {
        return a;
    }
}

Expr make_bitwise_not(Expr a, Expr) {
    return ~a;
}

Expr make_shift_right(Expr a, Expr b) {
    return a >> (b % a.type().bits());
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
            };

            static make_bin_op_fn make_rare_bin_op[] = {
                make_absd,
                make_bitwise_or,
                make_bitwise_and,
                make_bitwise_xor,
                make_bitwise_not,
                make_abs,
                make_shift_right,  // No shift left or we just keep testing integer overflow
            };

            Expr a = random_expr(rng, t, depth, overflow_undef);
            Expr b = random_expr(rng, t, depth, overflow_undef);
            if ((rng() & 7) == 0) {
                return random_choice(rng, make_rare_bin_op)(a, b);
            } else {
                return random_choice(rng, make_bin_op)(a, b);
            }
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

bool test_expression(std::mt19937 &rng, Expr test, int samples) {
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

}  // namespace

int main(int argc, char **argv) {
    // Depth of the randomly generated expression trees.
    constexpr int depth = 6;
    // Number of samples to test the generated expressions for.
    constexpr int samples = 3;

    std::random_device rd{};
    std::mt19937 seed_generator{rd()};

    for (int i = 0; (argc == 1) || i < ((argc == 1) ? 10000 : 1); i++) {
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
            printf("Testing subexpressions...\n");
            class TestSubexpressions : public IRMutator {
                std::mt19937 &rng;
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

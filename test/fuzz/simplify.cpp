#include "Halide.h"
#include "fuzz_helpers.h"
#include <array>
#include <functional>
#include <fuzzer/FuzzedDataProvider.h>
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

Expr random_var(FuzzedDataProvider &fdp) {
    int fuzz_count = fdp.ConsumeIntegralInRange<int>(0, fuzz_var_count - 1);
    return Variable::make(Int(0), fuzz_var(fuzz_count));
}

Type random_type(FuzzedDataProvider &fdp, int width) {
    Type t = fdp.PickValueInArray(fuzz_types);

    if (width > 1) {
        t = t.with_lanes(width);
    }
    return t;
}

int get_random_divisor(FuzzedDataProvider &fdp, Type t) {
    std::vector<int> divisors = {t.lanes()};
    for (int dd = 2; dd < t.lanes(); dd++) {
        if (t.lanes() % dd == 0) {
            divisors.push_back(dd);
        }
    }

    return pick_value_in_vector(fdp, divisors);
}

Expr random_leaf(FuzzedDataProvider &fdp, Type t, bool overflow_undef = false, bool imm_only = false) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }
    if (t.is_scalar()) {
        if (!imm_only && fdp.ConsumeBool()) {
            auto v1 = random_var(fdp);
            return cast(t, v1);
        } else {
            if (overflow_undef) {
                // For Int(32), we don't care about correctness during
                // overflow, so just use numbers that are unlikely to
                // overflow.
                return cast(t, fdp.ConsumeIntegralInRange<int>(-128, 127));
            } else {
                return cast(t, fdp.ConsumeIntegral<int>());
            }
        }
    } else {
        int lanes = get_random_divisor(fdp, t);
        if (fdp.ConsumeBool()) {
            auto e1 = random_leaf(fdp, t.with_lanes(t.lanes() / lanes), overflow_undef);
            auto e2 = random_leaf(fdp, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Ramp::make(e1, e2, lanes);
        } else {
            auto e1 = random_leaf(fdp, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Broadcast::make(e1, lanes);
        }
    }
}

Expr random_expr(FuzzedDataProvider &fdp, Type t, int depth, bool overflow_undef = false);

Expr random_condition(FuzzedDataProvider &fdp, Type t, int depth, bool maybe_scalar) {
    static make_bin_op_fn make_bin_op[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };

    if (maybe_scalar && fdp.ConsumeBool()) {
        t = t.element_of();
    }

    Expr a = random_expr(fdp, t, depth);
    Expr b = random_expr(fdp, t, depth);
    return fdp.PickValueInArray(make_bin_op)(a, b);
}

Expr make_absd(Expr a, Expr b) {
    // random_expr() assumes that the result t is the same as the input t,
    // which isn't true for all absd variants, so force the issue.
    return cast(a.type(), absd(a, b));
}

Expr random_expr(FuzzedDataProvider &fdp, Type t, int depth, bool overflow_undef) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }

    if (depth-- <= 0) {
        return random_leaf(fdp, t, overflow_undef);
    }

    std::function<Expr()> operations[] = {
        [&]() {
            return random_leaf(fdp, t);
        },
        [&]() {
            auto c = random_condition(fdp, t, depth, true);
            auto e1 = random_expr(fdp, t, depth, overflow_undef);
            auto e2 = random_expr(fdp, t, depth, overflow_undef);
            return Select::make(c, e1, e2);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(fdp, t);
                auto e1 = random_expr(fdp, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Broadcast::make(e1, lanes);
            }
            return random_expr(fdp, t, depth, overflow_undef);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(fdp, t);
                auto e1 = random_expr(fdp, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                auto e2 = random_expr(fdp, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Ramp::make(e1, e2, lanes);
            }
            return random_expr(fdp, t, depth, overflow_undef);
        },
        [&]() {
            if (t.is_bool()) {
                auto e1 = random_expr(fdp, t, depth);
                return Not::make(e1);
            }
            return random_expr(fdp, t, depth, overflow_undef);
        },
        [&]() {
            // When generating boolean expressions, maybe throw in a condition on non-bool types.
            if (t.is_bool()) {
                return random_condition(fdp, random_type(fdp, t.lanes()), depth, false);
            }
            return random_expr(fdp, t, depth, overflow_undef);
        },
        [&]() {
            // Get a random type that isn't t or int32 (int32 can overflow and we don't care about that).
            // Note also that the FuzzedDataProvider doesn't actually promise to return a random distribution --
            // it can (e.g.) decide to just return 0 for all data, forever -- so this loop has no guarantee
            // of eventually finding a different type. To remedy this, we'll just put a limit on the retries.
            int count = 0;
            Type subtype;
            do {
                subtype = random_type(fdp, t.lanes());
            } while (++count < 10 && (subtype == t || (subtype.is_int() && subtype.bits() == 32)));
            auto e1 = random_expr(fdp, subtype, depth, overflow_undef);
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

            Expr a = random_expr(fdp, t, depth, overflow_undef);
            Expr b = random_expr(fdp, t, depth, overflow_undef);
            return fdp.PickValueInArray(make_bin_op)(a, b);
        },
        [&]() {
            static make_bin_op_fn make_bin_op[] = {
                And::make,
                Or::make,
            };

            // Boolean operations -- both sides must be cast to booleans,
            // and then we must cast the result back to 't'.
            Expr a = random_expr(fdp, t, depth, overflow_undef);
            Expr b = random_expr(fdp, t, depth, overflow_undef);
            Type bool_with_lanes = Bool(t.lanes());
            a = cast(bool_with_lanes, a);
            b = cast(bool_with_lanes, b);
            return cast(t, fdp.PickValueInArray(make_bin_op)(a, b));
        }};
    return fdp.PickValueInArray(operations)();
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
            for (map<string, Expr>::const_iterator i = vars.begin(); i != vars.end(); i++) {
                std::cout << i->first << " = " << i->second << "\n";
            }

            std::cout << a << "\n";
            std::cout << b << "\n";
            std::cout << "In vector lane " << j << ":\n";
            std::cout << a_j << " -> " << a_j_v << "\n";
            std::cout << b_j << " -> " << b_j_v << "\n";
            return false;
        }
    }
    return true;
}

bool test_expression(FuzzedDataProvider &fdp, Expr test, int samples) {
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
                v->second = random_leaf(fdp, test.type().element_of(), true);
                iterations++;
            } while (expr_uses_var(v->second, v->first) && iterations < kMaxLeafIterations);
        }

        if (!test_simplification(test, simplified, test.type(), vars)) {
            return false;
        }
    }
    return true;
}

// These are here to enable copy of failed output expressions
// and pasting them into the test for debugging; they are commented out
// to avoid "unused function" warnings in some build environments.
#if 0
Expr ramp(Expr b, Expr s, int w) {
    return Ramp::make(b, s, w);
}
Expr x1(Expr x) {
    return Broadcast::make(x, 2);
}
Expr x2(Expr x) {
    return Broadcast::make(x, 2);
}
Expr x3(Expr x) {
    return Broadcast::make(x, 3);
}
Expr x4(Expr x) {
    return Broadcast::make(x, 4);
}
Expr x6(Expr x) {
    return Broadcast::make(x, 6);
}
Expr x8(Expr x) {
    return Broadcast::make(x, 8);
}
Expr uint1(Expr x) {
    return Cast::make(UInt(1), x);
}
Expr uint8(Expr x) {
    return Cast::make(UInt(8), x);
}
Expr uint16(Expr x) {
    return Cast::make(UInt(16), x);
}
Expr uint32(Expr x) {
    return Cast::make(UInt(32), x);
}
Expr int8(Expr x) {
    return Cast::make(Int(8), x);
}
Expr int16(Expr x) {
    return Cast::make(Int(16), x);
}
Expr int32(Expr x) {
    return Cast::make(Int(32), x);
}
Expr uint1x2(Expr x) {
    return Cast::make(UInt(1).with_lanes(2), x);
}
Expr uint8x2(Expr x) {
    return Cast::make(UInt(8).with_lanes(2), x);
}
Expr uint16x2(Expr x) {
    return Cast::make(UInt(16).with_lanes(2), x);
}
Expr uint32x2(Expr x) {
    return Cast::make(UInt(32).with_lanes(2), x);
}
Expr int8x2(Expr x) {
    return Cast::make(Int(8).with_lanes(2), x);
}
Expr int16x2(Expr x) {
    return Cast::make(Int(16).with_lanes(2), x);
}
Expr int32x2(Expr x) {
    return Cast::make(Int(32).with_lanes(2), x);
}
#endif

Expr a(Variable::make(Int(0), fuzz_var(0)));
Expr b(Variable::make(Int(0), fuzz_var(1)));
Expr c(Variable::make(Int(0), fuzz_var(2)));
Expr d(Variable::make(Int(0), fuzz_var(3)));
Expr e(Variable::make(Int(0), fuzz_var(4)));

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Depth of the randomly generated expression trees.
    const int depth = 5;
    // Number of samples to test the generated expressions for.
    const int samples = 3;

    FuzzedDataProvider fdp(data, size);

    std::array<int, 6> vector_widths = {1, 2, 3, 4, 6, 8};
    int width = fdp.PickValueInArray(vector_widths);
    Type VT = random_type(fdp, width);
    // Generate a random expr...
    Expr test = random_expr(fdp, VT, depth);
    assert(test_expression(fdp, test, samples));
    return 0;
}

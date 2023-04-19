#include "Halide.h"
#include <array>
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

const int fuzz_var_count = 5;

Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};
const int fuzz_type_count = sizeof(fuzz_types) / sizeof(fuzz_types[0]);

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

Expr random_var(FuzzedDataProvider &fdp) {
    int fuzz_count = fdp.ConsumeIntegralInRange<int>(0, fuzz_var_count);
    return Variable::make(Int(0), fuzz_var(fuzz_count));
}

Type random_type(FuzzedDataProvider &fdp, int width) {
    Type T = fdp.PickValueInArray(fuzz_types);

    if (width > 1) {
        T = T.with_lanes(width);
    }
    return T;
}

int get_random_divisor(FuzzedDataProvider &fdp, Type t) {
    std::vector<int> divisors = {t.lanes()};
    for (int dd = 2; dd < t.lanes(); dd++) {
        if (t.lanes() % dd == 0) {
            divisors.push_back(dd);
        }
    }

    return divisors[fdp.ConsumeIntegralInRange<size_t>(0, divisors.size() - 1)];
}

Expr random_leaf(FuzzedDataProvider &fdp, Type T, bool overflow_undef = false, bool imm_only = false) {
    if (T.is_int() && T.bits() == 32) {
        overflow_undef = true;
    }
    if (T.is_scalar()) {
        int var = fdp.ConsumeIntegralInRange<int>(0, fuzz_var_count) + 1;
        if (!imm_only && var < fuzz_var_count) {
            auto v1 = random_var(fdp);
            return cast(T, v1);
        } else {
            if (overflow_undef) {
                // For Int(32), we don't care about correctness during
                // overflow, so just use numbers that are unlikely to
                // overflow.
                return cast(T, fdp.ConsumeIntegralInRange<int>(-128, 128));
            } else {
                return cast(T, fdp.ConsumeIntegralInRange<int>(-RAND_MAX / 2, RAND_MAX / 2));
            }
        }
    } else {
        int lanes = get_random_divisor(fdp, T);
        if (fdp.ConsumeBool()) {
            auto e1 = random_leaf(fdp, T.with_lanes(T.lanes() / lanes), overflow_undef);
            auto e2 = random_leaf(fdp, T.with_lanes(T.lanes() / lanes), overflow_undef);
            return Ramp::make(e1, e2, lanes);
        } else {
            auto e1 = random_leaf(fdp, T.with_lanes(T.lanes() / lanes), overflow_undef);
            return Broadcast::make(e1, lanes);
        }
    }
}

Expr random_expr(FuzzedDataProvider &fdp, Type T, int depth, bool overflow_undef = false);

Expr random_condition(FuzzedDataProvider &fdp, Type T, int depth, bool maybe_scalar) {
    typedef Expr (*make_bin_op_fn)(Expr, Expr);
    static make_bin_op_fn make_bin_op[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };
    const int op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);

    if (maybe_scalar && fdp.ConsumeBool()) {
        T = T.element_of();
    }

    Expr a = random_expr(fdp, T, depth);
    Expr b = random_expr(fdp, T, depth);
    int op = fdp.ConsumeIntegralInRange<int>(0, op_count);
    return make_bin_op[op](a, b);
}

Expr make_absd(Expr a, Expr b) {
    // random_expr() assumes that the result type is the same as the input type,
    // which isn't true for all absd variants, so force the issue.
    return cast(a.type(), absd(a, b));
}

Expr random_expr(FuzzedDataProvider &fdp, Type T, int depth, bool overflow_undef) {
    typedef Expr (*make_bin_op_fn)(Expr, Expr);
    static make_bin_op_fn make_bin_op[] = {
        Add::make,
        Sub::make,
        Mul::make,
        Min::make,
        Max::make,
        Div::make,
        Mod::make,
        make_absd,
    };

    static make_bin_op_fn make_bool_bin_op[] = {
        And::make,
        Or::make,
    };

    if (T.is_int() && T.bits() == 32) {
        overflow_undef = true;
    }

    if (depth-- <= 0) {
        return random_leaf(fdp, T, overflow_undef);
    }

    const int bin_op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);
    const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
    const int op_count = bin_op_count + bool_bin_op_count + 5;

    int op = fdp.ConsumeIntegralInRange<int>(0, op_count);
    switch (op) {
    case 0:
        return random_leaf(fdp, T);
    case 1: {
        auto c = random_condition(fdp, T, depth, true);
        auto e1 = random_expr(fdp, T, depth, overflow_undef);
        auto e2 = random_expr(fdp, T, depth, overflow_undef);
        return Select::make(c, e1, e2);
    }
    case 2:
        if (T.lanes() != 1) {
            int lanes = get_random_divisor(fdp, T);
            auto e1 = random_expr(fdp, T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            return Broadcast::make(e1, lanes);
        }
        break;
    case 3:
        if (T.lanes() != 1) {
            int lanes = get_random_divisor(fdp, T);
            auto e1 = random_expr(fdp, T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            auto e2 = random_expr(fdp, T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            return Ramp::make(e1, e2, lanes);
        }
        break;

    case 4:
        if (T.is_bool()) {
            auto e1 = random_expr(fdp, T, depth);
            return Not::make(e1);
        }
        break;

    case 5:
        // When generating boolean expressions, maybe throw in a condition on non-bool types.
        if (T.is_bool()) {
            return random_condition(fdp, random_type(fdp, T.lanes()), depth, false);
        }
        break;

    case 6: {
        // Get a random type that isn't T or int32 (int32 can overflow and we don't care about that).
        Type subT;
        do {
            subT = random_type(fdp, T.lanes());
        } while (subT == T || (subT.is_int() && subT.bits() == 32));
        auto e1 = random_expr(fdp, subT, depth, overflow_undef);
        return Cast::make(T, e1);
    }

    default:
        make_bin_op_fn maker;
        if (T.is_bool()) {
            maker = make_bool_bin_op[op % bool_bin_op_count];
        } else {
            maker = make_bin_op[op % bin_op_count];
        }
        Expr a = random_expr(fdp, T, depth, overflow_undef);
        Expr b = random_expr(fdp, T, depth, overflow_undef);
        return maker(a, b);
    }
    // If we got here, try again.
    return random_expr(fdp, T, depth, overflow_undef);
}

bool test_simplification(Expr a, Expr b, Type T, const map<string, Expr> &vars) {
    for (int j = 0; j < T.lanes(); j++) {
        Expr a_j = a;
        Expr b_j = b;
        if (T.lanes() != 1) {
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

// These are here to enable copy of failed output expressions and pasting them into the test for debugging.
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

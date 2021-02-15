#include "Halide.h"
#include <array>
#include <random>
#include <stdio.h>
#include <time.h>

namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

const int fuzz_var_count = 5;

// use std::mt19937 instead of rand() to ensure consistent behavior on all systems
std::mt19937 rng(0);

Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};
const int fuzz_type_count = sizeof(fuzz_types) / sizeof(fuzz_types[0]);

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

static Type global_var_type = Int(32);

Expr random_var() {
    int fuzz_count = rng() % fuzz_var_count;
    return Variable::make(global_var_type, fuzz_var(fuzz_count));
}

Type random_type(int width) {
    Type T = fuzz_types[rng() % fuzz_type_count];

    if (width > 1) {
        T = T.with_lanes(width);
    }
    return T;
}

int get_random_divisor(Type t) {
    std::vector<int> divisors = {t.lanes()};
    for (int dd = 2; dd < t.lanes(); dd++) {
        if (t.lanes() % dd == 0) {
            divisors.push_back(dd);
        }
    }

    return divisors[rng() % divisors.size()];
}

Expr random_leaf(Type T, bool overflow_undef = false, bool imm_only = false) {
    if (T.is_int() && T.bits() == 32) {
        overflow_undef = true;
    }
    if (T.is_scalar()) {
        int var = rng() % fuzz_var_count + 1;
        if (!imm_only && var < fuzz_var_count) {
            auto v1 = random_var();
            return cast(T, v1);
        } else {
            // if (T.is_int()) {
            //     return cast(T, (int)(rng() % 256 - 128));
            // } else {
            //     return cast(T, (int)(rng() % 256));
            // }

            if (overflow_undef) {
                // For Int(32), we don't care about correctness during
                // overflow, so just use numbers that are unlikely to
                // overflow.
                return cast(T, (int)(rng() % 256 - 128));
            } else {
                return cast(T, (int)(rng() - RAND_MAX / 2));
            }
        }
    } else {
        int lanes = get_random_divisor(T);
        if (rng() % 2 == 0) {
            auto e1 = random_leaf(T.with_lanes(T.lanes() / lanes), overflow_undef);
            auto e2 = random_leaf(T.with_lanes(T.lanes() / lanes), overflow_undef);
            return Ramp::make(e1, e2, lanes);
        } else {
            auto e1 = random_leaf(T.with_lanes(T.lanes() / lanes), overflow_undef);
            return Broadcast::make(e1, lanes);
        }
    }
}

Expr random_expr(Type T, int depth, bool overflow_undef = false);

Expr random_condition(Type T, int depth, bool maybe_scalar) {
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

    if (maybe_scalar && rng() % T.lanes() == 0) {
        T = T.element_of();
    }

    Expr a = random_expr(T, depth);
    Expr b = random_expr(T, depth);
    int op = rng() % op_count;
    return make_bin_op[op](a, b);
}

// Expr make_absd(Expr a, Expr b) {
//     // random_expr() assumes that the result type is the same as the input type,
//     // which isn't true for all absd variants, so force the issue.
//     return cast(a.type(), absd(a, b));
// }

Expr random_expr(Type T, int depth, bool overflow_undef) {
    typedef Expr (*make_bin_op_fn)(Expr, Expr);
    static make_bin_op_fn make_bin_op[] = {
        Add::make,
        Sub::make,
        Mul::make,
        Min::make,
        Max::make,
        Div::make,
        Mod::make,
        // make_absd,
    };

    static make_bin_op_fn make_bool_bin_op[] = {
        And::make,
        Or::make,
    };

    if (T.is_int() && T.bits() == 32) {
        overflow_undef = true;
    }

    if (depth-- <= 0) {
        return random_leaf(T, overflow_undef);
    }

    const int bin_op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);
    const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
    const int op_count = bin_op_count + bool_bin_op_count + 5;

    int op = rng() % op_count;
    switch (op) {
    case 0:
        return random_leaf(T);
    case 1: {
        auto c = random_condition(T, depth, true);
        auto e1 = random_expr(T, depth, overflow_undef);
        auto e2 = random_expr(T, depth, overflow_undef);
        return Select::make(c, e1, e2);
    }
    case 2:
        if (T.lanes() != 1) {
            int lanes = get_random_divisor(T);
            auto e1 = random_expr(T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            return Broadcast::make(e1, lanes);
        }
        break;
    case 3:
        if (T.lanes() != 1) {
            int lanes = get_random_divisor(T);
            auto e1 = random_expr(T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            auto e2 = random_expr(T.with_lanes(T.lanes() / lanes), depth, overflow_undef);
            return Ramp::make(e1, e2, lanes);
        }
        break;

    case 4:
        if (T.is_bool()) {
            auto e1 = random_expr(T, depth);
            return Not::make(e1);
        }
        break;

    case 5:
        // When generating boolean expressions, maybe throw in a condition on non-bool types.
        if (T.is_bool()) {
            return random_condition(random_type(T.lanes()), depth, false);
        }
        break;

    case 6: {
        // Get a random type that isn't T or int32 (int32 can overflow and we don't care about that).
        Type subT;
        do {
            subT = random_type(T.lanes());
        } while (subT == T || (subT.is_int() && subT.bits() == 32));
        auto e1 = random_expr(subT, depth, overflow_undef);
        return Cast::make(T, e1);
    }

    default:
        make_bin_op_fn maker;
        if (T.is_bool()) {
            maker = make_bool_bin_op[op % bool_bin_op_count];
        } else {
            maker = make_bin_op[op % bin_op_count];
        }
        Expr a = random_expr(T, depth, overflow_undef);
        Expr b = random_expr(T, depth, overflow_undef);
        return maker(a, b);
    }
    // If we got here, try again.
    return random_expr(T, depth, overflow_undef);
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
    return Broadcast::make(x, 2);
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
Expr uint32x3(Expr x) {
    return Cast::make(UInt(32).with_lanes(3), x);
}
Expr int8x2(Expr x) {
    return Cast::make(Int(8).with_lanes(2), x);
}
Expr int16x2(Expr x) {
    return Cast::make(Int(16).with_lanes(2), x);
}
Expr int16x3(Expr x) {
    return Cast::make(Int(16).with_lanes(3), x);
}
Expr int32x2(Expr x) {
    return Cast::make(Int(32).with_lanes(2), x);
}

Expr a(Variable::make(global_var_type, fuzz_var(0)));
Expr b(Variable::make(global_var_type, fuzz_var(1)));
Expr c(Variable::make(global_var_type, fuzz_var(2)));
Expr d(Variable::make(global_var_type, fuzz_var(3)));
Expr e(Variable::make(global_var_type, fuzz_var(4)));

int random_in_range(int min_value, int max_value) {
    if (min_value == max_value) {
        return min_value;
    }
    return (rng() % (max_value - min_value)) + min_value;
}

std::ostream &operator<<(std::ostream &stream, const Interval &interval) {
    stream << "[" << interval.min << ", " << interval.max << "]";
    return stream;
}

Interval random_interval(Type T) {
    // TODO: does 1/4 chance of unbounded seem resonable or unreasonable?
    static int op_count = 4;
    // TODO: change these back to > 0
    // these are arbitrarily true for now.
    bool needs_min = (rng() % op_count >= 0);
    bool needs_max = (rng() % op_count >= 0);

    Interval interval;

    int min_value = -128;
    int max_value = 128;

    Type t = T.element_of();
    if ((t.is_uint() || (t.is_int() && t.bits() <= 16))) {
        if (auto ptr = as_const_int(t.min())) {
            min_value = *ptr;
        } else if (auto ptr = as_const_uint(t.min())) {
            min_value = *ptr;
        } else {
            std::cerr << "random_interval failed to find min of: " << T << "\n";
        }
        if (auto ptr = as_const_int(t.max())) {
            max_value = *ptr;
        } else if (auto ptr = as_const_uint(t.max())) {
            // can't represent all uint32_t in int type
            if (*ptr <= 128) {
                max_value = *ptr;
            }
        } else {
            std::cerr << "random_interval failed to find max of: " << T << "\n";
        }
    }

    // Try to get rid of very large values that might overflow.
    min_value = std::max(min_value, -128);
    max_value = std::min(max_value, 128);

    if (needs_min) {
        // change the min_value for the calculation of a max
        min_value = random_in_range(min_value, max_value);
        interval.min = cast(T, min_value);
    }

    if (needs_max) {
        max_value = random_in_range(min_value, max_value);
        interval.max = cast(T, max_value);
    }

    if (min_value > max_value || (interval.is_bounded() && can_prove(interval.min > interval.max))) {
        // TODO: this should be some kind of assert.
        std::cerr << "random_interval failed: ";
        std::cerr << min_value << " > " << max_value << "\n";
        std::cerr << interval.min << " > " << interval.max << "\n";
        std::cerr << interval << "\n";
    }

    return interval;
}

int sample_interval(const Interval &interval) {
    // TODO: Use other values?
    int min_value = -128;
    int max_value = 128;

    if (interval.has_lower_bound()) {
        if (auto ptr = as_const_int(interval.min)) {
            min_value = *ptr;
        } else if (auto ptr = as_const_uint(interval.min)) {
            min_value = *ptr;
        } else {
            std::cerr << "sample_interval (min) failed: " << interval << "\n";
            return 0;
        }
    }

    if (interval.has_upper_bound()) {
        if (auto ptr = as_const_int(interval.max)) {
            max_value = *ptr;
        } else if (auto ptr = as_const_uint(interval.max)) {
            max_value = *ptr;
        } else {
            std::cerr << "sample_interval (max) failed: " << interval << "\n";
            return 0;
        }
    }

    int value = random_in_range(min_value, max_value);
    // std::cerr << "sample_interval() = " << value << "\n";
    return value;
}

bool is_integer_overflow(const Expr &expr) {
    if (const Call *call = expr.as<Call>()) {
        return call->is_intrinsic(Call::signed_integer_overflow);
    } else {
        return false;
    }
}

bool test_bounds(Expr test, const Interval &interval, Type T, const map<string, Expr> &vars) {
    for (int j = 0; j < T.lanes(); j++) {
        Expr a_j = test;
        if (T.lanes() != 1) {
            a_j = extract_lane(test, j);
        }

        Expr a_j_v = simplify(substitute(vars, a_j));

        // TODO: handle a_j_v being integer overflow.

        // TODO: not sure what else to do here.
        if (interval.has_upper_bound()) {
            if (!can_prove(a_j_v <= interval.max)) {
                std::cerr << "can't prove upper bound: " << (a_j_v <= interval.max) << "\n";
                for (auto v = vars.begin(); v != vars.end(); v++) {
                    std::cerr << v->first << " = " << v->second << "\n";
                }

                std::cerr << test << "\n";
                std::cerr << interval << "\n";
                std::cerr << "In vector lane " << j << ":\n";
                std::cerr << a_j << " -> " << a_j_v << "\n";
                return false;
            }
        }

        if (interval.has_lower_bound()) {
            if (!can_prove(a_j_v >= interval.min)) {
                std::cerr << "can't prove lower bound: " << (a_j_v >= interval.min) << "\n";
                std::cerr << "Expr: " << test << "\n";
                std::cerr << "Interval: " << interval << "\n";

                for (auto v = vars.begin(); v != vars.end(); v++) {
                    std::cerr << v->first << " = " << v->second << "\n";
                }

                std::cerr << "In vector lane " << j << ":\n";
                std::cerr << a_j << " -> " << a_j_v << "\n";
                return false;
            }
        }
    }
    return true;
}

bool test_expression_bounds(Expr test, int trials, int samples_per_trial) {

    map<string, Expr> vars;
    for (int i = 0; i < fuzz_var_count; i++) {
        vars[fuzz_var(i)] = Expr();
    }

    for (int i = 0; i < trials; i++) {
        Scope<Interval> scope;

        for (auto v = vars.begin(); v != vars.end(); v++) {
            // TODO: why this type? It's what the simplifier fuzzer uses.
            Interval interval = random_interval(global_var_type);
            scope.push(v->first, interval);
        }

        Interval interval = bounds_of_expr_in_scope(test, scope);
        interval.min = simplify(interval.min);
        interval.max = simplify(interval.max);

        if (!(interval.has_upper_bound() || interval.has_lower_bound())) {
            // TODO: for now, return. Assumes that no other combo
            // will produce a bounded interval (not necessarily true).
            // This is to shorten the amount of output from this test.
            return true;  // any result is allowed
        }

        if ((interval.has_upper_bound() && is_integer_overflow(interval.max)) ||
            (interval.has_lower_bound() && is_integer_overflow(interval.min))) {
            // TODO: what do we do here?
            return true;
        }

        for (int j = 0; j < samples_per_trial; j++) {
            for (std::map<string, Expr>::iterator v = vars.begin(); v != vars.end(); v++) {
                Interval interval = scope.get(v->first);
                v->second = cast(global_var_type, sample_interval(interval));
            }

            if (!test_bounds(test, interval, test.type(), vars)) {
                std::cerr << "scope {"
                          << "\n";
                for (auto v = vars.begin(); v != vars.end(); v++) {
                    std::cerr << "\t" << v->first << " : " << scope.get(v->first) << "\n";
                }
                std::cerr << "}"
                          << "\n";
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    // Number of random expressions to test.
    const int count = 1000;
    // Depth of the randomly generated expression trees.
    const int depth = 3;
    // Number of trials to test the generated expressions for.
    const int trials = 10;
    // Number of samples of the intervals per trial to test.
    const int samples = 10;

    // We want different fuzz tests every time, to increase coverage.
    // We also report the seed to enable reproducing failures.
    int fuzz_seed = argc > 1 ? atoi(argv[1]) : time(nullptr);
    rng.seed(fuzz_seed);
    std::cout << "bounds inference fuzz test seed: " << fuzz_seed << "\n";

    std::array<int, 6> vector_widths = {1, 2, 3, 4, 6, 8};
    for (int n = 0; n < count; n++) {
        // int width = 1;
        int width = vector_widths[rng() % vector_widths.size()];
        // This is the type that will be the innermost (leaf) value type.
        Type expr_type = random_type(width);
        Type var_type = random_type(1);
        global_var_type = var_type;
        // Generate a random expr...
        Expr test = random_expr(expr_type, depth);
        if (!test_expression_bounds(test, trials, samples)) {
            return -1;
        }
    }

    std::cout << "Success!\n";
    return 0;
}

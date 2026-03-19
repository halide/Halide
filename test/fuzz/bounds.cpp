#include "Halide.h"
#include "fuzz_helpers.h"
#include <array>
#include <stdio.h>

namespace {

using std::map;
using std::string;
using namespace Halide;
using namespace Halide::Internal;

using make_bin_op_fn = Expr (*)(Expr, Expr);

constexpr int fuzz_var_count = 5;

Type fuzz_types[] = {UInt(1), UInt(8), UInt(16), UInt(32), Int(8), Int(16), Int(32)};

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

// This is modified for each round.
Type global_var_type = Int(32);

Expr random_var(FuzzingContext &fuzz) {
    int fuzz_count = fuzz.ConsumeIntegralInRange(0, fuzz_var_count - 1);
    return Variable::make(global_var_type, fuzz_var(fuzz_count));
}

Type random_type(FuzzingContext &fuzz, int width) {
    Type t = fuzz.PickValueInArray(fuzz_types);

    if (width > 1) {
        t = t.with_lanes(width);
    }
    return t;
}

int get_random_divisor(FuzzingContext &fuzz, Type t) {
    std::vector<int> divisors = {t.lanes()};
    for (int dd = 2; dd < t.lanes(); dd++) {
        if (t.lanes() % dd == 0) {
            divisors.push_back(dd);
        }
    }

    return fuzz.PickValueInVector(divisors);
}

Expr random_leaf(FuzzingContext &fuzz, Type t, bool overflow_undef = false, bool imm_only = false) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }
    if (t.is_scalar()) {
        if (!imm_only && fuzz.ConsumeBool()) {
            auto v1 = random_var(fuzz);
            return cast(t, v1);
        } else if (overflow_undef) {
            // For Int(32), we don't care about correctness during
            // overflow, so just use numbers that are unlikely to
            // overflow.
            return cast(t, fuzz.ConsumeIntegralInRange<int>(-128, 127));
        } else {
            return cast(t, fuzz.ConsumeIntegral<int>());
        }
    } else {
        int lanes = get_random_divisor(fuzz, t);
        if (fuzz.ConsumeBool()) {
            auto e1 = random_leaf(fuzz, t.with_lanes(t.lanes() / lanes), overflow_undef);
            auto e2 = random_leaf(fuzz, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Ramp::make(e1, e2, lanes);
        } else {
            auto e1 = random_leaf(fuzz, t.with_lanes(t.lanes() / lanes), overflow_undef);
            return Broadcast::make(e1, lanes);
        }
    }
}

Expr random_expr(FuzzingContext &fuzz, Type t, int depth, bool overflow_undef = false);

Expr random_condition(FuzzingContext &fuzz, Type t, int depth, bool maybe_scalar) {
    static make_bin_op_fn make_bin_op[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };

    if (maybe_scalar && fuzz.ConsumeBool()) {
        t = t.element_of();
    }

    Expr a = random_expr(fuzz, t, depth);
    Expr b = random_expr(fuzz, t, depth);
    return fuzz.PickValueInArray(make_bin_op)(a, b);
}

Expr random_expr(FuzzingContext &fuzz, Type t, int depth, bool overflow_undef) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }
    if (depth-- <= 0) {
        return random_leaf(fuzz, t, overflow_undef);
    }

    std::function<Expr()> operations[] = {
        [&]() {
            return random_leaf(fuzz, t);
        },
        [&]() {
            auto c = random_condition(fuzz, t, depth, true);
            auto e1 = random_expr(fuzz, t, depth, overflow_undef);
            auto e2 = random_expr(fuzz, t, depth, overflow_undef);
            // Don't use Select::make: we want to use select() here to
            // ensure that the condition and values match types.
            return select(c, e1, e2);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(fuzz, t);
                auto e1 = random_expr(fuzz, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Broadcast::make(e1, lanes);
            }
            // If we got here, try again.
            return random_expr(fuzz, t, depth, overflow_undef);
        },
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(fuzz, t);
                auto e1 = random_expr(fuzz, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                auto e2 = random_expr(fuzz, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Ramp::make(e1, e2, lanes);
            }
            // If we got here, try again.
            return random_expr(fuzz, t, depth, overflow_undef);
        },
        [&]() {
            if (t.is_bool()) {
                auto e1 = random_expr(fuzz, t, depth);
                return Not::make(e1);
            }
            // If we got here, try again.
            return random_expr(fuzz, t, depth, overflow_undef);
        },
        [&]() {
            if (t.is_bool()) {
                return random_condition(fuzz, random_type(fuzz, t.lanes()), depth, false);
            }
            // If we got here, try again.
            return random_expr(fuzz, t, depth, overflow_undef);
        },
        [&]() {
            // Get a random type that isn't t or int32 (int32 can overflow and we don't care about that).
            // Note also that the FuzzingContext doesn't actually promise to return a random distribution --
            // it can (e.g.) decide to just return 0 for all data, forever -- so this loop has no guarantee
            // of eventually finding a different type. To remedy this, we'll just put a limit on the retries.
            int count = 0;
            Type subtype;
            do {
                subtype = random_type(fuzz, t.lanes());
            } while (++count < 10 && (subtype == t || (subtype.is_int() && subtype.bits() == 32)));
            auto e1 = random_expr(fuzz, subtype, depth, overflow_undef);
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
            make_bin_op_fn maker = fuzz.PickValueInArray(make_bin_op);
            Expr a = random_expr(fuzz, t, depth, overflow_undef);
            Expr b = random_expr(fuzz, t, depth, overflow_undef);
            return maker(a, b);
        },
        [&]() {
            static make_bin_op_fn make_bin_op[] = {
                // Binary operations.
                And::make,
                Or::make,
            };
            // Boolean operations -- both sides must be cast to booleans,
            // and then we must cast the result back to 't'.
            make_bin_op_fn maker = fuzz.PickValueInArray(make_bin_op);
            Expr a = random_expr(fuzz, t, depth, overflow_undef);
            Expr b = random_expr(fuzz, t, depth, overflow_undef);
            Type bool_with_lanes = Bool(t.lanes());
            a = cast(bool_with_lanes, a);
            b = cast(bool_with_lanes, b);
            return cast(t, maker(a, b));
        },
    };
    return fuzz.PickValueInArray(operations)();
}

Interval random_interval(FuzzingContext &fuzz, Type t) {
    Interval interval;

    int min_value = -128;
    int max_value = 128;

    Type t_elem = t.element_of();
    if ((t_elem.is_uint() || (t_elem.is_int() && t_elem.bits() <= 16))) {
        Expr t_min = t_elem.min();
        Expr t_max = t_elem.max();
        if (auto ptr = as_const_int(t_min)) {
            min_value = *ptr;
        } else if (auto ptr = as_const_uint(t_min)) {
            min_value = *ptr;
        } else {
            debug(0) << "random_interval failed to find min of: " << t << "\n";
        }
        if (auto ptr = as_const_int(t_max)) {
            max_value = *ptr;
        } else if (auto ptr = as_const_uint(t_max)) {
            // can't represent all uint32_t in int type
            if (*ptr <= 128) {
                max_value = *ptr;
            }
        } else {
            debug(0) << "random_interval failed to find max of: " << t << "\n";
        }
    }

    // Try to get rid of very large values that might overflow.
    min_value = std::max(min_value, -128);
    max_value = std::min(max_value, 128);

    // change the min_value for the calculation of max
    min_value = fuzz.ConsumeIntegralInRange<int>(min_value, max_value);
    interval.min = cast(t, min_value);

    max_value = fuzz.ConsumeIntegralInRange<int>(min_value, max_value);
    interval.max = cast(t, max_value);

    if (min_value > max_value || (interval.is_bounded() && can_prove(interval.min > interval.max))) {
        debug(0) << "random_interval failed: ";
        debug(0) << min_value << " > " << max_value << "\n";
        debug(0) << interval.min << " > " << interval.max << "\n";
        debug(0) << interval << "\n";
        debug(0) << "random_interval failed\n";
        exit(1);
    }

    return interval;
}

int sample_interval(FuzzingContext &fuzz, const Interval &interval) {
    // Values chosen so intervals don't repeatedly produce signed_overflow when simplified.
    int min_value = -128;
    int max_value = 128;

    if (interval.has_lower_bound()) {
        if (auto ptr = as_const_int(interval.min)) {
            min_value = *ptr;
        } else if (auto ptr = as_const_uint(interval.min)) {
            min_value = *ptr;
        } else {
            debug(0) << "sample_interval (min) failed: " << interval.min << "\n";
            exit(1);
        }
    }

    if (interval.has_upper_bound()) {
        if (auto ptr = as_const_int(interval.max)) {
            max_value = *ptr;
        } else if (auto ptr = as_const_uint(interval.max)) {
            max_value = *ptr;
        } else {
            debug(0) << "sample_interval (max) failed: " << interval.max << "\n";
            exit(1);
        }
    }

    return fuzz.ConsumeIntegralInRange<int>(min_value, max_value);
}

bool test_bounds(const Expr &test, const Interval &interval, Type t, const map<string, Expr> &vars) {
    for (int j = 0; j < t.lanes(); j++) {
        Expr a_j = test;
        if (t.lanes() != 1) {
            a_j = extract_lane(test, j);
        }

        Expr a_j_v = simplify(substitute(vars, a_j));

        if (!is_const(a_j_v)) {
            // Probably overflow, abort.
            continue;
        }

        // This fuzzer only looks for constant bounds, otherwise it's probably overflow.
        if (interval.has_upper_bound()) {
            if (!can_prove(a_j_v <= interval.max)) {
                debug(0) << "can't prove upper bound: " << (a_j_v <= interval.max) << "\n";
                for (const auto &[var, val] : vars) {
                    debug(0) << var << " = " << val << "\n";
                }

                debug(0) << test << "\n";
                debug(0) << interval << "\n";
                debug(0) << "In vector lane " << j << ":\n";
                debug(0) << a_j << " -> " << a_j_v << "\n";
                return false;
            }
        }

        if (interval.has_lower_bound()) {
            if (!can_prove(a_j_v >= interval.min)) {
                debug(0) << "can't prove lower bound: " << (a_j_v >= interval.min) << "\n";
                debug(0) << "Expr: " << test << "\n";
                debug(0) << "Interval: " << interval << "\n";

                for (const auto &[var, val] : vars) {
                    debug(0) << var << " = " << val << "\n";
                }

                debug(0) << "In vector lane " << j << ":\n";
                debug(0) << a_j << " -> " << a_j_v << "\n";
                return false;
            }
        }
    }
    return true;
}

bool test_expression_bounds(FuzzingContext &fuzz, const Expr &test, int trials, int samples_per_trial) {
    map<string, Expr> vars;
    for (int i = 0; i < fuzz_var_count; i++) {
        vars[fuzz_var(i)] = Expr();
    }

    // Don't test expressions with potentially overflowing casts to signed
    // ints. This is known to be broken (See
    // https://github.com/halide/Halide/pull/7814)
    bool contains_risky_cast = false;
    visit_with(test, [&](auto *self, const auto *op) {
        if (!contains_risky_cast) {
            if constexpr (std::is_same_v<decltype(op), const Cast *>) {
                contains_risky_cast |= (op->type.is_int() &&
                                        op->type.bits() >= 32 &&
                                        !op->type.can_represent(op->value.type()));
            } else {
                self->visit_base(op);
            }
        }
    });
    if (contains_risky_cast) {
        return true;
    }

    for (int i = 0; i < trials; i++) {
        Scope<Interval> scope;

        for (auto &[var, val] : vars) {
            // This type is used because the variables will be this type for a given round.
            scope.push(var, random_interval(fuzz, global_var_type));
        }

        Interval interval = bounds_of_expr_in_scope(test, scope);
        interval.min = simplify(interval.min);
        interval.max = simplify(interval.max);

        if (!(interval.has_upper_bound() || interval.has_lower_bound())) {
            // For now, return. Assumes that no other combo
            // will produce a bounded interval (not necessarily true).
            // This is to shorten the amount of output from this test.
            return true;  // any result is allowed
        }

        if ((interval.has_upper_bound() && is_signed_integer_overflow(interval.max)) ||
            (interval.has_lower_bound() && is_signed_integer_overflow(interval.min))) {
            // Quit for now, assume other intervals will produce the same results.
            return true;
        }

        if (!is_const(interval.min) || !is_const(interval.max)) {
            // Likely signed_integer_overflow, give up now.
            return true;
        }

        for (int j = 0; j < samples_per_trial; j++) {
            for (auto &[var, val] : vars) {
                val = cast(global_var_type, sample_interval(fuzz, scope.get(var)));
            }

            if (!test_bounds(test, interval, test.type(), vars)) {
                debug(0) << "scope {\n";
                for (auto &[var, val] : vars) {
                    debug(0) << "\t" << var << " : " << scope.get(var) << "\n";
                }
                debug(0) << "}\n";
                return false;
            }
        }
    }
    return true;
}

}  // namespace

FUZZ_TEST(bounds, FuzzingContext &fuzz) {
    // Number of random expressions to test.
    constexpr int count = 100;
    // Depth of the randomly generated expression trees.
    constexpr int depth = 3;
    // Number of trials to test the generated expressions for.
    constexpr int trials = 10;
    // Number of samples of the intervals per trial to test.
    constexpr int samples = 10;

    for (int n = 0; n < count; n++) {
        int vector_width = fuzz.PickValueInArray({1, 2, 3, 4, 6, 8});
        // This is the type that will be the innermost (leaf) value type.
        Type expr_type = random_type(fuzz, vector_width);
        Type var_type = random_type(fuzz, 1);
        global_var_type = var_type;
        // Generate a random expr...
        Expr test = random_expr(fuzz, expr_type, depth);
        if (!test_expression_bounds(fuzz, test, trials, samples)) {
            return 1;
        }
    }

    return 0;
}

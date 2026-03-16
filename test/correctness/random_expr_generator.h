#include "Halide.h"

#include <array>
#include <functional>
#include <vector>
#include <random>

namespace Halide {
  namespace Internal {
    namespace Fuzz {

using namespace Halide;
using namespace Halide::Internal;
using RandomEngine = std::mt19937_64;
using namespace std;

using make_bin_op_fn = Expr (*)(Expr, Expr);

constexpr int fuzz_var_count = 5;
std::vector<Param<int>> fuzz_vars(fuzz_var_count);

template<typename T>
decltype(auto) random_choice(RandomEngine &rng, T &&choices) {
    std::uniform_int_distribution<size_t> dist(0, std::size(choices) - 1);
    return choices[dist(rng)];
}

Type fuzz_types[] = {UInt(8), UInt(16), UInt(32), UInt(64), Int(8), Int(16), Int(32), Int(64)};

Type random_scalar_type(RandomEngine &rng) {
    return random_choice(rng, fuzz_types);
}

int get_random_divisor(RandomEngine &rng, int x) {
    vector<int> divisors;
    divisors.reserve(x);
    for (int i = 2; i <= x; i++) {
        if (x % i == 0) {
            divisors.push_back(i);
        }
    }
    return random_choice(rng, divisors);
}

std::string fuzz_var(int i) {
    return std::string(1, 'a' + i);
}

Expr random_var(RandomEngine &rng, Type t) {
    std::uniform_int_distribution dist(0, fuzz_var_count - 1);
    int fuzz_count = dist(rng);
    return cast(t, Variable::make(Int(32), fuzz_var(fuzz_count)));
}

Type random_type(RandomEngine &rng, int width) {
    Type t = random_choice(rng, fuzz_types);
    if (width > 1) {
        t = t.with_lanes(width);
    }
    return t;
}


Expr random_const(RandomEngine &rng, Type t) {
    int val = (int)((int8_t)(rng() & 0x0f));
    if (t.is_vector()) {
        return Broadcast::make(cast(t.element_of(), val), t.lanes());
    } else {
        return cast(t, val);
    }
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


Expr random_leaf(RandomEngine &rng, Type t, bool overflow_undef = false, bool imm_only = false) {
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
        int lanes = get_random_divisor(rng, t.lanes());
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

Expr random_expr(RandomEngine &rng, Type t, int depth, bool overflow_undef = false);

Expr random_condition(RandomEngine &rng, Type t, int depth, bool maybe_scalar) {
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

Expr random_expr(RandomEngine &rng, Type t, int depth, bool overflow_undef) {
    if (t.is_int() && t.bits() == 32) {
        overflow_undef = true;
    }

    if (depth-- <= 0) {
        return random_leaf(rng, t, overflow_undef);
    }

    // Weight the choices to cover all Deinterleaver visit methods:
    // Broadcast, Ramp, Cast, Reinterpret, Call (via abs), Shuffle,
    // VectorReduce, Add/Sub/Min/Max (handled by default IRMutator)
    std::function<Expr()> ops[] = {
        // Leaf
        [&]() -> Expr {
            return random_leaf(rng, t);
        },
        // Arithmetic & Bitwise ops
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
        // Boolean ops
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
        },
        // Select
        [&]() -> Expr {
            auto c = random_condition(rng, t, depth, true);
            auto e1 = random_expr(rng, t, depth, overflow_undef);
            auto e2 = random_expr(rng, t, depth, overflow_undef);
            return select(c, e1, e2);
        },
        // Cast
#if 0
        [&]() {
            // Get a random type that isn't `t` or int32 (int32 can overflow, and we don't care about that).
            std::vector<Type> subtypes;
            for (const Type &subtype : fuzz_types) {
                if (subtype != t && subtype != Int(32)) {
                    subtypes.push_back(subtype);
                }
            }
            Type subtype = random_choice(rng, subtypes).with_lanes(t.lanes());
            return Cast::make(t, random_expr(rng, subtype, depth, overflow_undef));
        },
#endif
        // Reinterpret (different bit width, changes lane count)
        [&]() -> Expr {
            int total_bits = t.bits() * t.lanes();
            // Pick a different bit width that divides the total bits evenly
            int bit_widths[] = {8, 16, 32, 64};
            vector<int> valid_widths;
            for (int bw : bit_widths) {
                if (total_bits % bw == 0) {
                    valid_widths.push_back(bw);
                }
            }
            // Should at least be able to preserve the existing bit width and change signedness.
            internal_assert(!valid_widths.empty());
            int other_bits = random_choice(rng, valid_widths);
            int other_lanes = total_bits / other_bits;
            Type other = ((rng() & 1) ? Int(other_bits) : UInt(other_bits)).with_lanes(other_lanes);
            Expr e = random_expr(rng, other, depth);
            return Reinterpret::make(t, e);
        },
        // Broadcast of sub-expression
        [&]() -> Expr {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(rng, t.lanes());
                auto e1 = random_expr(rng, t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                return Broadcast::make(e1, lanes);
            }
            return random_expr(rng, t, depth, overflow_undef);
        },
        // Ramp
        [&]() {
            if (t.lanes() != 1) {
                int lanes = get_random_divisor(rng, t.lanes());
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
        // Shuffle (interleave)
        [&]() -> Expr {
            if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                int half = t.lanes() / 2;
                Expr a = random_expr(rng, t.with_lanes(half), depth);
                Expr b = random_expr(rng, t.with_lanes(half), depth);
                return Shuffle::make_interleave({a, b});
            }
            // Fall back to a simple expression
            return random_expr(rng, t, depth);
        },
        // Shuffle (concat)
        [&]() -> Expr {
            if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                int half = t.lanes() / 2;
                Expr a = random_expr(rng, t.with_lanes(half), depth);
                Expr b = random_expr(rng, t.with_lanes(half), depth);
                return Shuffle::make_concat({a, b});
            }
            return random_expr(rng, t, depth);
        },
        // Shuffle (slice)
        [&]() -> Expr {
            // Make a wider vector and slice it
            if (t.lanes() <= 8) {
                int wider = t.lanes() * 2;
                Expr e = random_expr(rng, t.with_lanes(wider), depth);
                // Slice: take every other element starting at 0 or 1
                int start = rng() & 1;
                return Shuffle::make_slice(e, start, 2, t.lanes());
            }
            return random_expr(rng, t, depth);
        },
        // VectorReduce (only when we can make it work with lane counts)
        [&]() -> Expr {
            // Input has more lanes, output has t.lanes() lanes
            // factor must divide input lanes, and input lanes = t.lanes() * factor
            int factor = (rng() % 3) + 2;
            int input_lanes = t.lanes() * factor;
            if (input_lanes <= 32) {
                VectorReduce::Operator ops[] = {
                    VectorReduce::Add,
                    VectorReduce::Min,
                    VectorReduce::Max,
                };
                auto op = random_choice(rng, ops);
                Expr val = random_expr(rng, t.with_lanes(input_lanes), depth);
                internal_assert(val.type().lanes() == input_lanes) << val;
                return VectorReduce::make(op, val, t.lanes());
            }
            return random_expr(rng, t, depth);
        },
        // Call node (using a pure intrinsic like absd)
        [&]() -> Expr {
            Expr a = random_expr(rng, t, depth);
            Expr b = random_expr(rng, t, depth);
            return cast(t, absd(a, b));
        },
    };

    Expr e = random_choice(rng, ops)();
    internal_assert(e.type() == t) << e.type() << " " << t << " " << e;
    return e;
}

}
}
}

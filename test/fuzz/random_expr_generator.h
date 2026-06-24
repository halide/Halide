#include "Halide.h"

#include <array>
#include <functional>
#include <vector>

#include "fuzz_helpers.h"

namespace Halide {
namespace Internal {

using namespace std;
using namespace Halide;
using namespace Halide::Internal;

class RandomExpressionGenerator {
public:
    using make_bin_op_fn = Expr (*)(Expr, Expr);

    bool gen_cast = true;
    bool gen_select = true;
    bool gen_arithmetic = true;
    bool gen_bitwise = true;
    bool gen_bool_ops = true;
    bool gen_reinterpret = true;
    bool gen_broadcast_of_vector = true;
    bool gen_ramp_of_vector = true;
    bool gen_shuffles = true;
    bool gen_vector_reduce = true;

    FuzzingContext &fuzz;

    std::vector<Type> fuzz_types = {UInt(1), UInt(8), UInt(16), UInt(32), UInt(64), Int(8), Int(16), Int(32), Int(64)};
    std::vector<Param<int>> fuzz_vars;

    explicit RandomExpressionGenerator(FuzzingContext &fuzz)
        : fuzz(fuzz) {
        for (int i = 0; i < 5; i++) {
            fuzz_vars.emplace_back("a" + std::to_string(i));
        }
    }

    Type random_scalar_type() {
        return fuzz.PickValueInVector(fuzz_types);
    }

    int get_random_divisor(int x) {
        vector<int> divisors;
        divisors.reserve(x);
        for (int i = 2; i <= x; i++) {
            if (x % i == 0) {
                divisors.push_back(i);
            }
        }
        return fuzz.PickValueInVector(divisors);
    }

    Expr random_var(Type t) {
        return cast(t, fuzz.PickValueInVector(fuzz_vars));
    }

    Type random_type(int width) {
        Type t = random_scalar_type();
        if (width > 1) {
            t = t.with_lanes(width);
        }
        return t;
    }

    Expr random_const(Type t) const {
        int val = fuzz.ConsumeIntegralInRange(0, 0x0f);
        if (t.is_vector()) {
            return Broadcast::make(cast(t.element_of(), val), t.lanes());
        } else {
            return cast(t, val);
        }
    }

    static Expr make_absd(Expr a, Expr b) {
        // random_expr() assumes that the result t is the same as the input t,
        // which isn't true for all absd variants, so force the issue.
        return cast(a.type(), absd(a, b));
    }

    static Expr make_bitwise_or(Expr a, Expr b) {
        return a | b;
    }

    static Expr make_bitwise_and(Expr a, Expr b) {
        return a & b;
    }

    static Expr make_bitwise_xor(Expr a, Expr b) {
        return a ^ b;
    }

    static Expr make_abs(Expr a, Expr) {
        if (!a.type().is_uint()) {
            return cast(a.type(), abs(a));
        } else {
            return a;
        }
    }

    static Expr make_bitwise_not(Expr a, Expr) {
        return ~a;
    }

    static Expr make_shift_right(Expr a, Expr b) {
        return a >> (b % a.type().bits());
    }

    Expr random_leaf(Type t, bool overflow_undef = false, bool imm_only = false) {
        if (t.is_int() && t.bits() == 32) {
            overflow_undef = true;
        }
        if (t.is_scalar()) {
            if (!imm_only && fuzz.ConsumeBool()) {
                return random_var(t);
            } else {
                if (overflow_undef) {
                    // For Int(32), we don't care about correctness during
                    // overflow, so just use numbers that are unlikely to
                    // overflow.
                    return cast(t, fuzz.ConsumeIntegralInRange<int>(0, 255));
                } else {
                    return cast(t, fuzz.ConsumeIntegral<int>());
                }
            }
        } else {
            int lanes = get_random_divisor(t.lanes());

            if (fuzz.ConsumeBool()) {
                auto e1 = random_leaf(t.with_lanes(t.lanes() / lanes), overflow_undef);
                auto e2 = random_leaf(t.with_lanes(t.lanes() / lanes), overflow_undef);
                return Ramp::make(e1, e2, lanes);
            } else {
                auto e1 = random_leaf(t.with_lanes(t.lanes() / lanes), overflow_undef);
                return Broadcast::make(e1, lanes);
            }
        }
    }

    // Expr random_expr( Type t, int depth, bool overflow_undef = false);

    Expr random_condition(Type t, int depth, bool maybe_scalar) {
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

        Expr a = random_expr(t, depth);
        Expr b = random_expr(t, depth);
        return fuzz.PickValueInArray(make_bin_op)(a, b);
    }

    Expr random_expr(Type t, int depth, bool overflow_undef = false) {
        if (t.is_int() && t.bits() == 32) {
            overflow_undef = true;
        }

        if (depth-- <= 0) {
            return random_leaf(t, overflow_undef);
        }

        // Weight the choices to cover all Deinterleaver visit methods:
        // Broadcast, Ramp, Cast, Reinterpret, Call (via abs), Shuffle,
        // VectorReduce, Add/Sub/Min/Max (handled by default IRMutator)
        std::vector<std::function<Expr()>> ops;

        // Leaf
        ops.push_back([&]() -> Expr {
            return random_leaf(t);
        });

        if (gen_arithmetic) {
            // Arithmetic
            ops.push_back([&] {
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
                    make_abs};
                Expr a = random_expr(t, depth, overflow_undef);
                Expr b = random_expr(t, depth, overflow_undef);
                return fuzz.PickValueInArray(make_bin_op)(a, b);
            });
        }
        if (gen_bitwise && !t.is_float()) {
            // Bitwise -- not valid on float types, so skip when t is float.
            ops.push_back([&] {
                static make_bin_op_fn make_bin_op[] = {
                    make_bitwise_or,
                    make_bitwise_and,
                    make_bitwise_xor,
                    make_bitwise_not,
                    make_shift_right,  // No shift left or we just keep testing integer overflow
                };

                Expr a = random_expr(t, depth, overflow_undef);
                Expr b = random_expr(t, depth, overflow_undef);
                return fuzz.PickValueInArray(make_bin_op)(a, b);
            });
        }
        if (gen_bool_ops) {
            // Boolean ops
            ops.push_back([&] {
                static make_bin_op_fn make_bin_op[] = {
                    And::make,
                    Or::make,
                };

                // Boolean operations -- both sides must be cast to booleans,
                // and then we must cast the result back to 't'.
                Expr a = random_expr(t, depth, overflow_undef);
                Expr b = random_expr(t, depth, overflow_undef);
                Type bool_with_lanes = Bool(t.lanes());
                a = cast(bool_with_lanes, a);
                b = cast(bool_with_lanes, b);
                return cast(t, fuzz.PickValueInArray(make_bin_op)(a, b));
            });
        }
        if (gen_select) {
            // Select
            ops.push_back(
                [&]() -> Expr {
                    auto c = random_condition(t, depth, true);
                    auto e1 = random_expr(t, depth, overflow_undef);
                    auto e2 = random_expr(t, depth, overflow_undef);
                    return select(c, e1, e2);
                });
        }
        // Cast
        if (gen_cast) {
            ops.emplace_back([&] {
                // Get a random type that isn't `t` or int32 (int32 can overflow, and we don't care about that).
                std::vector<Type> subtypes;
                for (const Type &subtype : fuzz_types) {
                    if (subtype != t && subtype != Int(32)) {
                        subtypes.push_back(subtype);
                    }
                }
                Type subtype = fuzz.PickValueInVector(subtypes).with_lanes(t.lanes());
                return Cast::make(t, random_expr(subtype, depth, overflow_undef));
            });
        }
        if ((t.bits() * t.lanes() % 8 == 0) && gen_reinterpret) {
            // Reinterpret (different bit width, changes lane count)
            ops.push_back([&]() -> Expr {
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
                internal_assert(!valid_widths.empty()) << t;
                int other_bits = fuzz.PickValueInVector(valid_widths);
                int other_lanes = total_bits / other_bits;
                Type other = (fuzz.ConsumeBool() ? Int(other_bits) : UInt(other_bits)).with_lanes(other_lanes);
                Expr e = random_expr(other, depth);
                return Reinterpret::make(t, e);
            });
        }

        if (gen_broadcast_of_vector) {
            // Broadcast of vector
            ops.push_back([&]() -> Expr {
                if (t.lanes() != 1) {
                    int lanes = get_random_divisor(t.lanes());
                    auto e1 = random_expr(t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                    return Broadcast::make(e1, lanes);
                }
                return random_expr(t, depth, overflow_undef);
            });
        }

        if (gen_ramp_of_vector) {
            // Ramp
            ops.push_back([&] {
                if (t.lanes() != 1) {
                    int lanes = get_random_divisor(t.lanes());
                    auto e1 = random_expr(t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                    auto e2 = random_expr(t.with_lanes(t.lanes() / lanes), depth, overflow_undef);
                    return Ramp::make(e1, e2, lanes);
                }
                return random_expr(t, depth, overflow_undef);
            });
        }
        if (gen_bool_ops) {
            ops.push_back([&] {
                if (t.is_bool()) {
                    auto e1 = random_expr(t, depth);
                    return Not::make(e1);
                }
                return random_expr(t, depth, overflow_undef);
            });
            ops.push_back([&] {
                // When generating boolean expressions, maybe throw in a condition on non-bool types.
                if (t.is_bool()) {
                    return random_condition(random_type(t.lanes()), depth, false);
                }
                return random_expr(t, depth, overflow_undef);
            });
        }
        if (gen_shuffles) {
            // Shuffle (interleave)
            ops.push_back([&]() -> Expr {
                if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                    int half = t.lanes() / 2;
                    Expr a = random_expr(t.with_lanes(half), depth);
                    Expr b = random_expr(t.with_lanes(half), depth);
                    return Shuffle::make_interleave({a, b});
                }
                // Fall back to a simple expression
                return random_expr(t, depth);
            });
            // Shuffle (concat)
            ops.push_back([&]() -> Expr {
                if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                    int half = t.lanes() / 2;
                    Expr a = random_expr(t.with_lanes(half), depth);
                    Expr b = random_expr(t.with_lanes(half), depth);
                    return Shuffle::make_concat({a, b});
                }
                return random_expr(t, depth);
            });
            // Shuffle (slice)
            ops.push_back([&]() -> Expr {
                // Make a wider vector and slice it
                if (t.lanes() <= 8) {
                    int wider = t.lanes() * 2;
                    Expr e = random_expr(t.with_lanes(wider), depth);
                    // Slice: take every other element starting at 0 or 1
                    int start = fuzz.ConsumeIntegralInRange(0, 1);
                    return Shuffle::make_slice(e, start, 2, t.lanes());
                }
                return random_expr(t, depth);
            });
        }
        if (gen_vector_reduce) {
            // VectorReduce (only when we can make it work with lane counts)
            ops.push_back([&]() -> Expr {
                // Input has more lanes, output has t.lanes() lanes
                // factor must divide input lanes, and input lanes = t.lanes() * factor
                int factor = fuzz.ConsumeIntegralInRange(2, 4);
                int input_lanes = t.lanes() * factor;
                if (input_lanes <= 32) {
                    auto op =
                        t.is_bool() ?
                            fuzz.PickValueInArray({VectorReduce::And, VectorReduce::Or}) :
                            fuzz.PickValueInArray({VectorReduce::Add, VectorReduce::Min, VectorReduce::Max});
                    Expr val = random_expr(t.with_lanes(input_lanes), depth);
                    internal_assert(val.type().lanes() == input_lanes) << val;
                    return VectorReduce::make(op, val, t.lanes());
                }
                return random_expr(t, depth);
            });
        }

        Expr e = fuzz.PickValueInVector(ops)();
        internal_assert(e.type() == t) << e.type() << " " << t << " " << e;
        return e;
    }
};
}  // namespace Internal
}  // namespace Halide

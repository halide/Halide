#include "XtensaOptimize.h"
#include "AlignLoads.h"
#include "Bounds.h"
#include "CSE.h"
#include "ConciseCasts.h"
#include "Expr.h"
#include "ExprUsesVar.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Lerp.h"
#include "LoopCarry.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::vector;

using namespace Halide::ConciseCasts;

struct Pattern {
    enum Flags {
        InterleaveResult = 1 << 0,  // After evaluating the pattern, interleave native vectors of the result.
        SwapOps01 = 1 << 1,         // Swap operands 0 and 1 prior to substitution.
        SwapOps12 = 1 << 2,         // Swap operands 1 and 2 prior to substitution.
        ExactLog2Op1 = 1 << 3,      // Replace operand 1 with its log base 2, if the log base 2 is exact.
        ExactLog2Op2 = 1 << 4,      // Save as above, but for operand 2.

        BeginExactLog2Op = 1,  // BeginExactLog2Op and EndExactLog2Op ensure that we check only op1 and op2
        EndExactLog2Op = 3,    // for ExactLog2Op

        DeinterleaveOp0 = 1 << 5,  // Prior to evaluating the pattern, deinterleave native vectors of operand 0.
        DeinterleaveOp1 = 1 << 6,  // Same as above, but for operand 1.
        DeinterleaveOp2 = 1 << 7,
        DeinterleaveOps = DeinterleaveOp0 | DeinterleaveOp1 | DeinterleaveOp2,

        BeginDeinterleaveOp = 0,  // BeginDeinterleaveOp and EndDeinterleaveOp ensure that we check only three
        EndDeinterleaveOp = 3,    // deinterleave Op0, 1 and 2.
        // Many patterns are instructions that widen only
        // operand 0, which need to both deinterleave operand 0, and then
        // re-interleave the result.
        ReinterleaveOp0 = InterleaveResult | DeinterleaveOp0,

        NarrowOp0 = 1 << 10,  // Replace operand 0 with its half-width equivalent.
        NarrowOp1 = 1 << 11,  // Same as above, but for operand 1.
        NarrowOp2 = 1 << 12,
        NarrowOp3 = 1 << 13,
        NarrowOp4 = 1 << 14,
        NarrowOps = NarrowOp0 | NarrowOp1 | NarrowOp2 | NarrowOp3 | NarrowOp4,

        NarrowUnsignedOp0 = 1 << 15,  // Similar to the above, but narrow to an unsigned half width type.
        NarrowUnsignedOp1 = 1 << 16,
        NarrowUnsignedOp2 = 1 << 17,
        NarrowUnsignedOp3 = 1 << 18,
        NarrowUnsignedOp4 = 1 << 19,

        NarrowUnsignedOps = NarrowUnsignedOp0 | NarrowUnsignedOp1 | NarrowUnsignedOp2 | NarrowUnsignedOp3 | NarrowUnsignedOp4,

        AccumulatorOutput24 = 1 << 20,
        AccumulatorOutput48 = 1 << 21,
        AccumulatorOutput64 = 1 << 22,

        PassOnlyOp0 = 1 << 23,
        PassOnlyOp1 = 1 << 24,
        PassOnlyOp2 = 1 << 25,
        PassOnlyOp3 = 1 << 26,

        PassOps = PassOnlyOp0 | PassOnlyOp1 | PassOnlyOp2 | PassOnlyOp3,
        BeginPassOnlyOp = 0,  // BeginPassOnlyOp and EndPassOnlyOp ensure that we check only
        EndPassOnlyOp = 4,    // PassOps[0|1|2|3].
    };

    std::string intrin;  // Name of the intrinsic
    Expr pattern;        // The pattern to match against
    int flags;

    Pattern() = default;
    Pattern(const std::string &intrin, Expr p, int flags = 0)
        : intrin(intrin), pattern(std::move(p)), flags(flags) {
    }
};

Expr wild_u8 = Variable::make(UInt(8), "*");
Expr wild_u16 = Variable::make(UInt(16), "*");
Expr wild_u32 = Variable::make(UInt(32), "*");
Expr wild_u64 = Variable::make(UInt(64), "*");
Expr wild_i8 = Variable::make(Int(8), "*");
Expr wild_i16 = Variable::make(Int(16), "*");
Expr wild_i32 = Variable::make(Int(32), "*");
Expr wild_i64 = Variable::make(Int(64), "*");

Expr wild_u1x = Variable::make(Type(Type::UInt, 1, 0), "*");
Expr wild_u8x = Variable::make(Type(Type::UInt, 8, 0), "*");
Expr wild_u16x = Variable::make(Type(Type::UInt, 16, 0), "*");
Expr wild_u32x = Variable::make(Type(Type::UInt, 32, 0), "*");
Expr wild_u64x = Variable::make(Type(Type::UInt, 64, 0), "*");
Expr wild_i8x = Variable::make(Type(Type::Int, 8, 0), "*");
Expr wild_i16x = Variable::make(Type(Type::Int, 16, 0), "*");
Expr wild_i24x = Variable::make(Type(Type::Int, 24, 0), "*");
Expr wild_i32x = Variable::make(Type(Type::Int, 32, 0), "*");
Expr wild_i48x = Variable::make(Type(Type::Int, 48, 0), "*");
Expr wild_i64x = Variable::make(Type(Type::Int, 64, 0), "*");

// Broadcast to an unknown number of lanes, for making patterns.
Expr bc(Expr x) {
    return Broadcast::make(std::move(x), 0);
}

Expr vector_reduce(VectorReduce::Operator op, Expr x) {
  return VectorReduce::make(op, x, 0);
}

// Check if the matches satisfy the given pattern flags, and mutate the matches
// as specified by the flags.
bool process_match_flags(vector<Expr> &matches, int flags) {
    // The Pattern::Narrow*Op* flags are ordered such that the operand
    // corresponds to the bit (with operand 0 corresponding to the least
    // significant bit), so we can check for them all in a loop.
    for (size_t i = 0; i < matches.size(); i++) {
        Type t = matches[i].type();
        Type target_t = t.with_bits(t.bits() / 2);
        if (flags & (Pattern::NarrowOp0 << i)) {
            matches[i] = lossless_cast(target_t, matches[i]);
        } else if (flags & (Pattern::NarrowUnsignedOp0 << i)) {
            matches[i] = lossless_cast(target_t.with_code(Type::UInt), matches[i]);
        }
        if (!matches[i].defined()) return false;
    }

    for (size_t i = Pattern::BeginExactLog2Op; i < Pattern::EndExactLog2Op; i++) {
        // This flag is mainly to capture shifts. When the operand of a div or
        // mul is a power of 2, we can use a shift instead.
        if (flags & (Pattern::ExactLog2Op1 << (i - Pattern::BeginExactLog2Op))) {
            int pow;
            if (is_const_power_of_two_integer(matches[i], &pow)) {
                matches[i] = cast(matches[i].type().with_lanes(1), pow);
            } else {
                return false;
            }
        }
    }

    // for (size_t i = Pattern::BeginDeinterleaveOp; i < Pattern::EndDeinterleaveOp; i++) {
    //     if (flags & (Pattern::DeinterleaveOp0 << (i - Pattern::BeginDeinterleaveOp))) {
    //         internal_assert(matches[i].type().is_vector());
    //         matches[i] = native_deinterleave(matches[i]);
    //     }
    // }

    if (flags & Pattern::PassOps) {
        vector<Expr> new_matches;
        for (size_t i = Pattern::BeginPassOnlyOp; i < Pattern::EndPassOnlyOp; i++) {
            if (flags & (Pattern::PassOnlyOp0 << (i - Pattern::BeginPassOnlyOp))) {
                new_matches.push_back(matches[i]);
            }
        }
        matches.swap(new_matches);
    }

    if (flags & Pattern::SwapOps01) {
        internal_assert(matches.size() >= 2);
        std::swap(matches[0], matches[1]);
    }
    if (flags & Pattern::SwapOps12) {
        internal_assert(matches.size() >= 3);
        std::swap(matches[1], matches[2]);
    }
    return true;
}

// Replace an expression with the one specified by a pattern.
Expr replace_pattern(Expr x, const vector<Expr> &matches, const Pattern &p) {
    x = Call::make(x.type(), p.intrin, matches, Call::PureExtern);
    // if (p.flags & Pattern::InterleaveResult) {
    //     // The pattern wants us to interleave the result.
    //     x = native_interleave(x);
    // }
    return x;
}
// Attempt to apply one of the patterns to x. If a match is
// successful, the expression is replaced with a call using the
// matched operands. Prior to substitution, the matches are mutated
// with op_mutator.
Expr apply_patterns(Expr x, const vector<Pattern> &patterns, IRMutator *op_mutator) {
    debug(3) << "apply_patterns " << x << "\n";
    vector<Expr> matches;
    for (const Pattern &p : patterns) {
        if (expr_match(p.pattern, x, matches)) {
            debug(3) << "matched " << p.pattern << "\n";
            debug(3) << "to " << x << "\n";
            debug(3) << "matches:\n";
            for (Expr i : matches) {
                debug(3) << i << "\n";
            }

            if (!process_match_flags(matches, p.flags)) {
                continue;
            }

            // // Don't apply pattern if it involves an interleave,
            // // and is not a multiple of two vectors.
            // // See https://github.com/halide/Halide/issues/1582
            // if ((p.flags & Pattern::InterleaveResult) && !is_double_vector(x, target)) {
            //     continue;
            // }
            // Mutate the operands with the given mutator.
            for (Expr &op : matches) {
                op = op_mutator->mutate(op);
            }

            Type old_type = x.type();
            if (p.flags & Pattern::AccumulatorOutput48) {
                x = cast(Type(Type::Int, 48, x.type().lanes()), x);
            } else if (p.flags & Pattern::AccumulatorOutput64) {
                x = cast(Type(Type::Int, 64, x.type().lanes()), x);
            }
            x = replace_pattern(x, matches, p);
            if ((p.flags & Pattern::AccumulatorOutput48) || (p.flags & Pattern::AccumulatorOutput64)) {
                x = cast(old_type, x);
            }

            debug(3) << "rewrote to: " << x << "\n";
            return x;
        }
    }
    return x;
}

template<typename T>
Expr apply_commutative_patterns(const T *op, const vector<Pattern> &patterns, IRMutator *mutator) {
    Expr ret = apply_patterns(op, patterns, mutator);
    if (!ret.same_as(op)) return ret;

    // Try commuting the op
    Expr commuted = T::make(op->b, op->a);
    ret = apply_patterns(commuted, patterns, mutator);
    if (!ret.same_as(commuted)) return ret;

    return op;
}

class MatchXtensaPatterns : public IRGraphMutator {
private:
    using IRGraphMutator::visit;

    static Expr halide_xtensa_widen_mul_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_mul_i48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_mul_add_i48(Expr v0, Expr v1, Expr v2) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_mul_add_i48", {std::move(v0), std::move(v1), std::move(v2)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_add_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_add_i48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_widen_add_u48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_widen_add_u48", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_narrow_with_shift_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_narrow_with_shift_i16", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_narrow_with_shift_u16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_u16x.type(), "halide_xtensa_narrow_with_shift_u16", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_narrow_clz_i16(Expr v0) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_narrow_clz_i16", {std::move(v0)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_sat_add_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_sat_add_i16", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_sat_sub_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_sat_sub_i16", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_avg_round_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_avg_round_i16", {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_i32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_u32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u32x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_i16(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_slice_to_native_u16(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u16x.type(), "halide_xtensa_slice_to_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i16x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u16(Expr v0, Expr v1) {
        Expr call = Call::make(wild_u16x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i32(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i32(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_i32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)},
                               Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u32(Expr v0, Expr v1) {
        Expr call = Call::make(wild_u32x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_u1(Expr v0, Expr v1, Expr v2, Expr v3) {
        Expr call = Call::make(wild_u1x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1), std::move(v2), std::move(v3)}, Call::PureExtern);
        return call;
    }

    static Expr halide_xtensa_concat_from_native_i48(Expr v0, Expr v1) {
        Expr call = Call::make(wild_i48x.type(), "halide_xtensa_concat_from_native",
                               {std::move(v0), std::move(v1)}, Call::PureExtern);
        return call;
    }

    Expr visit(const Add *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> adds = {
                {"halide_xtensa_widen_pair_mul_i48", wild_i32x * wild_i32x + wild_i32x * wild_i32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_mul_u48", wild_u32x * wild_u32x + wild_u32x * wild_u32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},

                // Multiply-add to accumulator type.
                {"halide_xtensa_widen_pair_mul_add_i48", i32(halide_xtensa_widen_mul_add_i48(wild_i48x, wild_i16x, wild_i16x)) + i32(halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)), Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_mul_add_i48", i32(wild_i48x) + i32(halide_xtensa_widen_mul_i48(wild_i16x, wild_i16x)), Pattern::AccumulatorOutput48},
                // Add to accumulator type.
                // Paired add.
                {"halide_xtensa_widen_pair_add_i48", i32(halide_xtensa_widen_add_i48(wild_i48x, wild_i16x)) + wild_i16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_add_i48", i32(halide_xtensa_widen_add_i48(wild_i48x, wild_i16x)) + wild_i32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp2},
                {"halide_xtensa_widen_pair_add_u48", u32(halide_xtensa_widen_add_u48(wild_i48x, wild_u16x)) + wild_u16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_pair_add_u48", u32(halide_xtensa_widen_add_u48(wild_i48x, wild_u16x)) + wild_u32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp2},
                // Single add.
                {"halide_xtensa_widen_add_i48", i32(wild_i48x) + wild_i16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_add_i48", i32(wild_i48x) + wild_i32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp1},
                {"halide_xtensa_widen_add_u48", u32(wild_i48x) + wild_u16x, Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_add_u48", u32(wild_i48x) + wild_u32x, Pattern::AccumulatorOutput48 | Pattern::NarrowOp1},

                // Widening addition
                {"halide_xtensa_widen_add_u48", wild_u32x + wild_u32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_add_i48", wild_i32x + wild_i32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},

                {"halide_xtensa_widen_mul_add_i64", wild_i64x * wild_i64x + wild_i64x, Pattern::NarrowOps | Pattern::AccumulatorOutput64},

                // Predicated addition
                // {"halide_xtensa_pred_add_i16", wild_i16x + select(wild_u1x, wild_i16x, wild_i16x)}
            };

            Expr new_expr = apply_commutative_patterns(op, adds, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> subs = {
                // {"halide_xtensa_pred_sub_i16", wild_i16x - select(wild_u1x, wild_i16x, wild_i16x)}
            };

            Expr new_expr = apply_patterns(op, subs, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Mul *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> scalar_muls = {};

            static const std::vector<Pattern> muls = {
                // Widening multiplication
                {"halide_xtensa_widen_mul_i48", wild_i32x * bc(wild_i32), Pattern::NarrowOps | Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_mul_u48", wild_u32x * wild_u32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},
                {"halide_xtensa_widen_mul_i48", wild_i32x * wild_i32x, Pattern::NarrowOps | Pattern::AccumulatorOutput48},

                {"halide_xtensa_widen_mul_i64", wild_i64x * wild_i64x, Pattern::NarrowOps | Pattern::AccumulatorOutput64},
            };

            Expr new_expr = apply_commutative_patterns(op, scalar_muls, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }

            new_expr = apply_commutative_patterns(op, muls, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Div *op) override {
        if (op->type.is_vector()) {
            Expr div = op;
            static const std::vector<Pattern> divs = {
                // TODO(vksnk): Before enabling it add a check for ExactLogOp
                // {"halide_xtensa_div_i32_i16", wild_i32x / wild_i32x, Pattern::NarrowOp1}
            };

            Expr new_expr = apply_patterns(div, divs, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Max *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> maxes = {
                // {"halide_xtensa_pred_max_i16", max(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))}
            };

            Expr new_expr = apply_commutative_patterns(op, maxes, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Min *op) override {
        if (op->type.is_vector()) {
            static const std::vector<Pattern> maxes = {
                // {"halide_xtensa_pred_min_i16", max(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))}
            };

            Expr new_expr = apply_commutative_patterns(op, maxes, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const LT *op) override {
        static const vector<Pattern> lts = {
            // {"halide_xtensa_i48x_gt_zero", 0 < i32(wild_i48x)},
            // {"halide_xtensa_i48x_gt_zero", 0 < u32(wild_i48x)},
        };

        if (op->type.is_vector()) {
            Expr lt = op;

            std::vector<Expr> matches;

            Expr new_expr = apply_patterns(lt, lts, this);
            if (!new_expr.same_as(lt)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Cast *op) override {
        static const std::vector<Pattern> casts = {
            // Averaging
            {"halide_xtensa_avg_u16", u16((wild_u32x + wild_u32x) / 2), Pattern::NarrowOps},
            {"halide_xtensa_avg_i16", i16((wild_i32x + wild_i32x) / 2), Pattern::NarrowOps},

            {"halide_xtensa_avg_round_u16", u16((wild_u32x + wild_u32x + 1) / 2), Pattern::NarrowOps},
            {"halide_xtensa_avg_round_i16", i16((wild_i32x + wild_i32x + 1) / 2), Pattern::NarrowOps},

            // Saturating add/subtract
            {"halide_xtensa_sat_add_i16", i16_sat(wild_i32x + wild_i32x), Pattern::NarrowOps},
            {"halide_xtensa_sat_add_i32", i32_sat(wild_i64x + wild_i64x), Pattern::NarrowOps},
            {"halide_xtensa_sat_sub_i16", i16_sat(wild_i32x - wild_i32x), Pattern::NarrowOps},

            // Narrowing multiply with shift.
            // {"halide_xtensa_sat_mul_with_shift_i32", i32(wild_i64x * wild_i64x / wild_i64), Pattern::NarrowOp0 | Pattern::NarrowUnsignedOp1 | Pattern::ExactLog2Op2},

            // Narrowing with shifting.
            {"halide_xtensa_narrow_with_shift_i16", i16(wild_i32x >> wild_i32)},
            {"halide_xtensa_narrow_with_shift_i16", i16(wild_i32x / wild_i32), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_with_shift_u16", u16(wild_i32x >> wild_i32)},
            {"halide_xtensa_narrow_with_shift_u16", u16(wild_i32x / wild_i32), Pattern::ExactLog2Op1},

            {"halide_xtensa_narrow_high_i32", i32(wild_i64x >> 32)},
            {"halide_xtensa_narrow_high_i32", i32(wild_i64x / Expr(4294967296))},

            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x >> bc(wild_i64))},
            {"halide_xtensa_sat_narrow_shift_i32", i32_sat(wild_i64x / bc(wild_i64)), Pattern::ExactLog2Op1},

            // Concat and cast.
            {"halide_xtensa_convert_concat_i16_to_i8", i8(halide_xtensa_concat_from_native_i16(wild_i16x, wild_i16x))},
            {"halide_xtensa_convert_concat_i16_to_u8", u8(halide_xtensa_concat_from_native_i16(wild_i16x, wild_i16x))},
            {"halide_xtensa_convert_concat_u16_to_i8", i8(halide_xtensa_concat_from_native_u16(wild_u16x, wild_u16x))},
            {"halide_xtensa_convert_concat_u16_to_u8", u8(halide_xtensa_concat_from_native_u16(wild_u16x, wild_u16x))},
            {"halide_xtensa_convert_concat_i32_to_i16", i16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x))},
            {"halide_xtensa_convert_concat_i32_to_u16", u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x))},
            {"halide_xtensa_convert_concat_u32_to_i16", i16(halide_xtensa_concat_from_native_u32(wild_u32x, wild_u32x))},
            {"halide_xtensa_convert_concat_u32_to_u16", u16(halide_xtensa_concat_from_native_u32(wild_u32x, wild_u32x))},

            // {"halide_xtensa_narrow_clz_i16", i16(count_leading_zeros(wild_u32x))},
            // {"halide_xtensa_narrow_clz_i16", i16(count_leading_zeros(wild_i32x))},
        };
        if (op->type.is_vector()) {
            Expr cast = op;

            std::vector<Expr> matches;

            Expr new_expr = apply_patterns(cast, casts, this);
            if (!new_expr.same_as(cast)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        // TODO(vksnk): clean-up this if.
        if (op->is_interleave() && op->type.is_int_or_uint() && (op->type.bits() == 16) && (op->type.lanes() == 64)) {
            if (op->type.is_int()) {
                return Call::make(op->type, "halide_xtensa_interleave_i16",
                                  {mutate(op->vectors[0]), mutate(op->vectors[1])},
                                  Call::PureExtern);
            } else if (op->type.is_uint()) {
                return Call::make(op->type, "halide_xtensa_interleave_u16",
                                  {mutate(op->vectors[0]), mutate(op->vectors[1])},
                                  Call::PureExtern);
            }
        } else if (op->is_slice() && (op->slice_stride() == 1) && op->type.is_int() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            if (op->slice_begin() < 5) {
                return Call::make(op->type, "halide_xtensa_slice_start_" + std::to_string(op->slice_begin()) + "_i16",
                                  {mutate(op->vectors[0])},
                                  Call::PureExtern);
            } else {
                return Call::make(op->type, "halide_xtensa_slice_i16",
                                  {mutate(op->vectors[0]), op->slice_begin()},
                                  Call::PureExtern);
            }
        } else if (op->is_slice() && (op->slice_stride() == 1) && op->type.is_uint() && (op->type.bits() == 8) && (op->type.lanes() == 64)) {
            // Specialize slices which begin from 1, 2, 3 or 4.
            if (op->slice_begin() < 5) {
                return Call::make(op->type, "halide_xtensa_slice_start_" + std::to_string(op->slice_begin()) + "_u8",
                                  {mutate(op->vectors[0])},
                                  Call::PureExtern);
            } else {
                return Call::make(op->type, "halide_xtensa_slice_u8",
                                  {mutate(op->vectors[0]), op->slice_begin()},
                                  Call::PureExtern);
            }
        } else if (op->is_slice() && (op->slice_stride() == 1) && op->type.is_float() && (op->type.bits() == 32) && (op->type.lanes() == 16)) {
            return Call::make(op->type, "halide_xtensa_slice_f32",
                                {mutate(op->vectors[0]), op->slice_begin()},
                                Call::PureExtern);
        } else if (op->type.is_int_or_uint() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
            if ((op->vectors.size() == 1) && (op->vectors[0].type().lanes() == 64)) {
                bool is_deinterleave_even = true;
                for (int ix = 0; ix < (int)op->indices.size(); ix++) {
                    is_deinterleave_even = is_deinterleave_even && (op->indices[ix] == 2 * ix);
                }

                if (is_deinterleave_even) {
                    if (op->type.is_int()) {
                        return Call::make(op->type, "halide_xtensa_deinterleave_even_i16",
                                        {mutate(op->vectors[0])},
                                        Call::PureExtern);
                    } else if (op->type.is_uint()) {
                        return Call::make(op->type, "halide_xtensa_deinterleave_even_u16",
                                        {mutate(op->vectors[0])},
                                        Call::PureExtern);
                    }
                }
                bool is_deinterleave_odd = true;
                for (int ix = 0; ix < (int)op->indices.size(); ix++) {
                    is_deinterleave_odd = is_deinterleave_odd && (op->indices[ix] == 2 * ix + 1);
                }

                if (is_deinterleave_odd) {
                    if (op->type.is_int()) {
                        return Call::make(op->type, "halide_xtensa_deinterleave_odd_i16",
                                        {mutate(op->vectors[0])},
                                        Call::PureExtern);
                    } else if (op->type.is_uint()) {
                        return Call::make(op->type, "halide_xtensa_deinterleave_odd_u16",
                                        {mutate(op->vectors[0])},
                                        Call::PureExtern);
                    }
                }
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        // if (op->is_intrinsic(Call::lerp) && op->type.is_int() && (op->type.bits() == 16) && (op->type.lanes() == 32)) {
        //   internal_assert(op->args.size() == 3);
        //   // debug(0) << "Lerp - " << op->args[0] << " " << op->args[1] << " " << op->args[2] << "\n";
        //   // debug(0) << "Lerp types - " << op->args[0].type() << " " << op->args[1].type() << " " << op->args[2].type() << "\n";
        //   Expr weight = mutate(op->args[2]);
        //   const Broadcast* maybe_bc = weight.as<Broadcast>();
        //   if (maybe_bc) {
        //     weight = maybe_bc->value;
        //   }
        //   return Call::make(op->type, "halide_xtensa_lerp_i16",
        //                     {mutate(op->args[0]), mutate(op->args[1]), weight},
        //                     Call::PureExtern);
        // } else
        if (op->is_intrinsic(Call::lerp)) {
            // We need to lower lerps now to optimize the arithmetic
            // that they generate.
            internal_assert(op->args.size() == 3);
            return mutate(lower_lerp(op->args[0], op->args[1], op->args[2]));
        } else if (op->is_intrinsic(Call::absd) && op->type.is_vector() && op->type.is_uint() && (op->type.bits() == 16)) {
            internal_assert(op->args.size() == 2);
            return Call::make(op->type, "halide_xtensa_absd_i16",
                              {mutate(op->args[0]), mutate(op->args[1])},
                              Call::PureExtern);
        }

        static const std::vector<Pattern> calls = {
            // Narrowing with shifting.
            {"halide_xtensa_narrow_i48x_with_shift_i16", halide_xtensa_narrow_with_shift_i16(i32(wild_i48x), wild_i32)},
            {"halide_xtensa_narrow_i48x_with_shift_u16", halide_xtensa_narrow_with_shift_u16(i32(wild_i48x), wild_i32)},
            {"halide_xtensa_i48x_clz_i16", halide_xtensa_narrow_clz_i16(i32(wild_i48x))},
            {"halide_xtensa_i48x_clz_i16", halide_xtensa_narrow_clz_i16(u32(wild_i48x))},
            // Slice and convert
            {"halide_xtensa_convert_u8_low_u16", halide_xtensa_slice_to_native_u16(u16(wild_u8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_high_u16", halide_xtensa_slice_to_native_u16(u16(wild_u8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_low_i16", halide_xtensa_slice_to_native_i16(i16(wild_u8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_u8_high_i16", halide_xtensa_slice_to_native_i16(i16(wild_u8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_low_u16", halide_xtensa_slice_to_native_u16(u16(wild_i8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_high_u16", halide_xtensa_slice_to_native_u16(u16(wild_i8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_low_i16", halide_xtensa_slice_to_native_i16(i16(wild_i8x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i8_high_i16", halide_xtensa_slice_to_native_i16(i16(wild_i8x), 1, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i32_u16", halide_xtensa_slice_to_native_u16(u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x, wild_i32x, wild_i32x)), 0, 32, 64), Pattern::PassOnlyOp0 | Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i32_u16", halide_xtensa_slice_to_native_u16(u16(halide_xtensa_concat_from_native_i32(wild_i32x, wild_i32x, wild_i32x, wild_i32x)), 1, 32, 64), Pattern::PassOnlyOp2 | Pattern::PassOnlyOp3},

            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(wild_i48x), 0, 16, 32)},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(wild_i48x), 1, 16, 32)},
            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 0, 16, 64), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 1, 16, 64), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_i48_low_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 2, 16, 64), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i48_high_i32", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_i48(wild_i48x, wild_i48x)), 3, 16, 64), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_i48_low_u32", halide_xtensa_slice_to_native_u32(u32(wild_i48x), 0, 16, 32)},
            {"halide_xtensa_convert_i48_high_u32", halide_xtensa_slice_to_native_u32(u32(wild_i48x), 1, 16, 32)},
            {"halide_xtensa_convert_i16_low_i32", halide_xtensa_slice_to_native_i32(i32(wild_i16x), 0, wild_i32, wild_i32)},
            {"halide_xtensa_convert_i16_high_i32", halide_xtensa_slice_to_native_i32(i32(wild_i16x), 1, wild_i32, wild_i32)},

            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 0, 16, 64), Pattern::PassOnlyOp0},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 1, 16, 64), Pattern::PassOnlyOp1},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 2, 16, 64), Pattern::PassOnlyOp2},
            {"halide_xtensa_convert_to_int32x16_t_from_uint1x16_t", halide_xtensa_slice_to_native_i32(i32(halide_xtensa_concat_from_native_u1(wild_u1x, wild_u1x, wild_u1x, wild_u1x)), 3, 16, 64), Pattern::PassOnlyOp3},

            // {"halide_xtensa_avg121_round_i16", halide_xtensa_avg_round_i16(halide_xtensa_avg_round_i16(wild_i16x, wild_i16x), wild_i16x)},
            // Predicated saturated add/sub.
            // {"halide_xtensa_pred_sat_add_i16", halide_xtensa_sat_add_i16(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))},
            // {"halide_xtensa_pred_sat_sub_i16", halide_xtensa_sat_sub_i16(wild_i16x, select(wild_u1x, wild_i16x, wild_i16x))},
        };
        if (op->type.is_vector()) {
            Expr call = op;

            std::vector<Expr> matches;

            Expr new_expr = apply_patterns(call, calls, this);
            if (!new_expr.same_as(call)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    Expr visit(const VectorReduce* op) {
        // Full reduction.
        if (op->type.is_scalar()) {
            static const std::vector<Pattern> reduces = {
                {"halide_xtensa_full_reduce_i16", vector_reduce(VectorReduce::Add, wild_i32x), Pattern::NarrowOps},
            };

            Expr new_expr = apply_patterns(op, reduces, this);
            if (!new_expr.same_as(op)) {
                return new_expr;
            }
        }

        return IRGraphMutator::visit(op);
    }

    int loop_depth_ = 0;

    Stmt visit(const For *op) override {
        loop_depth_++;
        Stmt body = IRGraphMutator::visit(op);
        loop_depth_--;
        return body;
    }

    Stmt visit(const LetStmt *op) override {
        if (loop_depth_ < 1) {
            return IRGraphMutator::visit(op);
        }

        if (op->value.type().is_handle()) {
            return IRGraphMutator::visit(op);
        }

        if (op->value.type().is_scalar()) {
            return IRGraphMutator::visit(op);
        }
        Stmt body = op->body;
        body = substitute(op->name, op->value, body);
        return mutate(body);
    }

public:
    MatchXtensaPatterns() {
    }
};

// Find an upper bound of bounds.max - bounds.min.
Expr span_of_bounds(const Interval &bounds) {
    internal_assert(bounds.is_bounded());

    const Min *min_min = bounds.min.as<Min>();
    const Max *min_max = bounds.min.as<Max>();
    const Min *max_min = bounds.max.as<Min>();
    const Max *max_max = bounds.max.as<Max>();
    const Add *min_add = bounds.min.as<Add>();
    const Add *max_add = bounds.max.as<Add>();
    const Sub *min_sub = bounds.min.as<Sub>();
    const Sub *max_sub = bounds.max.as<Sub>();

    if (min_min && max_min && equal(min_min->b, max_min->b)) {
        return span_of_bounds({min_min->a, max_min->a});
    } else if (min_max && max_max && equal(min_max->b, max_max->b)) {
        return span_of_bounds({min_max->a, max_max->a});
    } else if (min_add && max_add && equal(min_add->b, max_add->b)) {
        return span_of_bounds({min_add->a, max_add->a});
    } else if (min_sub && max_sub && equal(min_sub->b, max_sub->b)) {
        return span_of_bounds({min_sub->a, max_sub->a});
    } else {
        return bounds.max - bounds.min;
    }
}

// Replace indirect loads with dynamic_shuffle intrinsics where
// possible.
class OptimizeShuffles : public IRMutator {
    int lut_alignment;
    Scope<Interval> bounds;
    std::vector<std::pair<std::string, Expr>> lets;

    using IRMutator::visit;

    template<typename NodeType, typename T>
    NodeType visit_let(const T *op) {
        // We only care about vector lets.
        if (op->value.type().is_vector()) {
            bounds.push(op->name, bounds_of_expr_in_scope(op->value, bounds));
        }
        NodeType node = IRMutator::visit(op);
        if (op->value.type().is_vector()) {
            bounds.pop(op->name);
        }
        return node;
    }

    Expr visit(const Let *op) override {
        lets.emplace_back(op->name, op->value);
        Expr expr = visit_let<Expr>(op);
        lets.pop_back();
        return expr;
    }
    Stmt visit(const LetStmt *op) override {
        return visit_let<Stmt>(op);
    }

    Expr visit(const Load *op) override {
        if (!is_one(op->predicate)) {
            // TODO(psuriana): We shouldn't mess with predicated load for now.
            return IRMutator::visit(op);
        }
        if (!op->type.is_vector() || op->index.as<Ramp>()) {
            // Don't handle scalar or simple vector loads.
            return IRMutator::visit(op);
        }

        Expr index = mutate(op->index);
        Interval unaligned_index_bounds = bounds_of_expr_in_scope(index, bounds);
        if (unaligned_index_bounds.is_bounded()) {
            // We want to try both the unaligned and aligned
            // bounds. The unaligned bounds might fit in 64 elements,
            // while the aligned bounds do not.
            int align = lut_alignment / op->type.bytes();
            Interval aligned_index_bounds = {
                (unaligned_index_bounds.min / align) * align,
                ((unaligned_index_bounds.max + align) / align) * align - 1};
            ModulusRemainder alignment(align, 0);

            for (Interval index_bounds : {aligned_index_bounds, unaligned_index_bounds}) {
                Expr index_span = span_of_bounds(index_bounds);
                index_span = common_subexpression_elimination(index_span);
                index_span = simplify(index_span);

                if (can_prove(index_span < 64)) {
                    // This is a lookup within an up to 64 element array. We
                    // can use dynamic_shuffle for this.
                    // TODO(vksnk): original code doesn't align/pad here, why?
                    int const_extent = as_const_int(index_span) ? (((*as_const_int(index_span) + align) / align) * align) : 64;
                    Expr base = simplify(index_bounds.min);

                    debug(0) << "const_extent - " << const_extent << "\n";
                    // Load all of the possible indices loaded from the
                    // LUT. Note that for clamped ramps, this loads up to 1
                    // vector past the max. CodeGen_Hexagon::allocation_padding
                    // returns a native vector size to account for this.
                    Expr lut = Load::make(op->type.with_lanes(const_extent), op->name,
                                          Ramp::make(base, 1, const_extent),
                                          op->image, op->param, const_true(const_extent), alignment);

                    // We know the size of the LUT is not more than 64, so we
                    // can safely cast the index to 16 bit, which
                    // dynamic_shuffle requires.
                    index = simplify(cast(Int(op->type.bits()).with_lanes(op->type.lanes()), index - base));
                    return Call::make(op->type, "halide_xtensa_dynamic_shuffle", {lut, index, 0, const_extent - 1}, Call::PureExtern);
                }
                // Only the first iteration of this loop is aligned.
                alignment = ModulusRemainder();
            }
        }
        if (!index.same_as(op->index)) {
            return Load::make(op->type, op->name, index, op->image, op->param, op->predicate, op->alignment);
        } else {
            return op;
        }
    }

public:
    OptimizeShuffles(int lut_alignment)
        : lut_alignment(lut_alignment) {
    }
};

class SplitVectorsToNativeSizes : public IRMutator {
private:
    std::vector<std::pair<Type, Type>> types_to_split;

    using IRMutator::visit;

    // Checks the list of types_to_split and returns native vector width for this
    // type if found and 0 otherwise.
    int get_native_vector_lanes_num(const Type &type) {
        for (const auto &t : types_to_split) {
            if (t.first == type) {
                return t.second.lanes();
            }
        }
        return 0;
    }

    Expr visit(const Broadcast *op) override {
        int native_lanes = get_native_vector_lanes_num(op->type);
        if (native_lanes > 0) {
            int split_to = op->type.lanes() / native_lanes;
            Expr value = mutate(op->value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr r = Broadcast::make(value, native_lanes);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Select *op) override {
        int native_lanes = get_native_vector_lanes_num(op->type);
        if (native_lanes > 0) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;
            Expr cond = mutate(op->condition);
            Expr t = mutate(op->true_value);
            Expr f = mutate(op->false_value);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced_cond = Call::make(cond.type().with_lanes(native_lanes),
                                              "halide_xtensa_slice_to_native",
                                              {cond, ix, native_lanes, total_lanes},
                                              Call::PureExtern);
                Expr sliced_t = Call::make(t.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {t, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr sliced_f = Call::make(f.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {f, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr r = Select::make(sliced_cond, sliced_t, sliced_f);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        return IRMutator::visit(op);
    }

    //     Expr visit(const Load* op) {
    //         Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    //         if (dense_ramp_base.defined()) {
    //             Expr predicate = mutate(op->predicate);
    //             Expr ramp_base = mutate(op->index.as<Ramp>()->base);
    //             Expr index = Ramp::make(ramp_base, 1, op->index.type().lanes());
    //             return Load::make(op->type, op->name, std::move(index),
    //                               op->image, op->param, std::move(predicate),
    //                               op->alignment);
    //         }
    //         return IRMutator::visit(op);
    //     }

    //     Stmt visit(const Store* op) {
    //         Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    //         if (dense_ramp_base.defined()) {
    //             Expr predicate = mutate(op->predicate);
    //             Expr value = mutate(op->value);
    //             Expr ramp_base = mutate(op->index.as<Ramp>()->base);
    //             Expr index = Ramp::make(ramp_base, 1, op->index.type().lanes());
    //             return Store::make(op->name, std::move(value), std::move(index), op->param, std::move(predicate), op->alignment);
    //         }
    //         return IRMutator::visit(op);
    //     }

    //     Expr visit(const Ramp *op) override {
    //         int native_lanes = get_native_vector_lanes_num(op->type);
    //         if (native_lanes > 0) {
    //             int split_to = op->type.lanes() / native_lanes;
    //             Expr base = mutate(op->base);
    //             Expr stride = mutate(op->stride);

    //             std::vector<Expr> concat_args;
    //             for (int ix = 0; ix < split_to; ix++) {
    //                 Expr r = Ramp::make(base + stride * (native_lanes * ix), stride, native_lanes);
    //                 concat_args.push_back(std::move(r));
    //             }
    //             return Call::make(op->type,
    //                               "halide_xtensa_concat_from_native",
    //                               concat_args, Call::PureExtern);
    //         }

    //         return IRMutator::visit(op);
    //     }

    template<typename Op>
    Expr visit_binop(const Op *op) {
        int native_lanes = get_native_vector_lanes_num(op->a.type());
        if (native_lanes > 0) {
            const int total_lanes = op->type.lanes();
            int split_to = op->type.lanes() / native_lanes;
            Expr a = mutate(op->a);
            Expr b = mutate(op->b);

            std::vector<Expr> concat_args;
            for (int ix = 0; ix < split_to; ix++) {
                Expr sliced_a = Call::make(a.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {a, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr sliced_b = Call::make(b.type().with_lanes(native_lanes),
                                           "halide_xtensa_slice_to_native",
                                           {b, ix, native_lanes, total_lanes},
                                           Call::PureExtern);
                Expr r = Op::make(sliced_a, sliced_b);
                concat_args.push_back(std::move(r));
            }
            return Call::make(op->type,
                              "halide_xtensa_concat_from_native",
                              concat_args, Call::PureExtern);
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Add *op) override {
        return visit_binop(op);
    }

    Expr visit(const Sub *op) override {
        return visit_binop(op);
    }

    Expr visit(const Mul *op) override {
        return visit_binop(op);
    }

    Expr visit(const Div *op) override {
        return visit_binop(op);
    }

    Expr visit(const Mod *op) override {
        return visit_binop(op);
    }

    Expr visit(const Min *op) override {
        return visit_binop(op);
    }

    Expr visit(const Max *op) override {
        return visit_binop(op);
    }

    Expr visit(const EQ *op) override {
        return visit_binop(op);
    }

    Expr visit(const NE *op) override {
        return visit_binop(op);
    }

    Expr visit(const LT *op) override {
        return visit_binop(op);
    }

    Expr visit(const LE *op) override {
        return visit_binop(op);
    }

    Expr visit(const GT *op) override {
        return visit_binop(op);
    }

    Expr visit(const GE *op) override {
        return visit_binop(op);
    }

    Expr visit(const Or *op) override {
        return visit_binop(op);
    }

    Expr visit(const And *op) override {
        return visit_binop(op);
    }

    Expr visit(const Call *op) override {
        int native_lanes = get_native_vector_lanes_num(op->type);
        if (native_lanes > 0) {
            if (!(op->name == "halide_xtensa_interleave_i16")) {
                const int total_lanes = op->type.lanes();
                int split_to = op->type.lanes() / native_lanes;
                vector<Expr> args;
                for (size_t arg_index = 0; arg_index < op->args.size(); arg_index++) {
                    args.push_back(mutate(op->args[arg_index]));
                }

                std::vector<Expr> concat_args;
                for (int ix = 0; ix < split_to; ix++) {
                    std::vector<Expr> sliced_args;
                    for (size_t arg_index = 0; arg_index < op->args.size(); arg_index++) {
                        Expr sliced_arg;
                        if (args[arg_index].type().is_scalar()) {
                            sliced_arg = args[arg_index];
                        } else {
                            sliced_arg = Call::make(args[arg_index].type().with_lanes(native_lanes),
                                                    "halide_xtensa_slice_to_native",
                                                    {args[arg_index], ix, native_lanes, total_lanes},
                                                    Call::PureExtern);
                        }
                        sliced_args.push_back(sliced_arg);
                    }

                    Expr r = Call::make(op->type.with_lanes(native_lanes), op->name, sliced_args, op->call_type);
                    concat_args.push_back(std::move(r));
                }
                return Call::make(op->type,
                                  "halide_xtensa_concat_from_native",
                                  concat_args, Call::PureExtern);
            }
        }

        return IRMutator::visit(op);
    }

public:
    SplitVectorsToNativeSizes() {
        types_to_split = {
            //{Type(Type::UInt, 1, 64), Type(Type::UInt, 1, 32)},
            {Type(Type::Int, 16, 64), Type(Type::Int, 16, 32)},
            {Type(Type::UInt, 16, 64), Type(Type::UInt, 16, 32)},
            {Type(Type::Int, 32, 32), Type(Type::Int, 32, 16)},
            {Type(Type::UInt, 32, 32), Type(Type::UInt, 32, 16)},
            {Type(Type::Int, 32, 64), Type(Type::Int, 32, 16)},
            {Type(Type::UInt, 32, 64), Type(Type::UInt, 32, 16)},
            {Type(Type::Int, 48, 64), Type(Type::Int, 48, 32)},
            {Type(Type::Int, 64, 32), Type(Type::Int, 64, 16)},
            {Type(Type::Int, 64, 64), Type(Type::Int, 64, 16)},
        };
    }
};

class SimplifySliceConcat : public IRGraphMutator {
private:
    using IRGraphMutator::visit;

    Expr visit(const Call *op) override {
        if (op->name == "halide_xtensa_slice_to_native") {
            Expr first_arg = mutate(op->args[0]);
            const Call *maybe_concat_call = first_arg.as<Call>();
            int slice_index = op->args[1].as<IntImm>()->value;
            int native_lanes = op->args[2].as<IntImm>()->value;
            int total_lanes = op->args[3].as<IntImm>()->value;
            if (maybe_concat_call && (maybe_concat_call->name == "halide_xtensa_concat_from_native")
                && (maybe_concat_call->type.lanes() == total_lanes) && ((int)maybe_concat_call->args.size() == total_lanes / native_lanes)) {
                return maybe_concat_call->args[slice_index];
            }
            const Shuffle* maybe_concat_shuffle = first_arg.as<Shuffle>();
            if (maybe_concat_shuffle
                  && maybe_concat_shuffle->is_concat()
                  && ((int)maybe_concat_shuffle->vectors.size() == total_lanes / native_lanes)
                  && ((int)maybe_concat_shuffle->vectors[slice_index].type().lanes() == native_lanes)
               ) {
                return maybe_concat_shuffle->vectors[slice_index];
            }

            if (first_arg.type().is_bool() && first_arg.type().is_scalar()) {
                return first_arg;
            }

            return Call::make(op->type, op->name,
                              {first_arg, op->args[1], op->args[2], op->args[3]},
                              Call::PureExtern);
        }

        return IRGraphMutator::visit(op);
    }

public:
    SimplifySliceConcat() {
    }
};

/** If an integer expression varies linearly with the variables in the
 * scope, return the linear term. Otherwise return an undefined
 * Expr. */
Expr is_linear(const Expr &e, const Scope<Expr> &linear) {
    if (e.type() != Int(32)) {
        return Expr();
    }
    if (const Variable *v = e.as<Variable>()) {
        if (linear.contains(v->name)) {
            return linear.get(v->name);
        } else {
            return make_zero(v->type);
        }
    } else if (const IntImm *op = e.as<IntImm>()) {
        return make_zero(op->type);
    } else if (const Add *add = e.as<Add>()) {
        Expr la = is_linear(add->a, linear);
        Expr lb = is_linear(add->b, linear);
        if (is_zero(lb)) {
            return la;
        } else if (is_zero(la)) {
            return lb;
        } else if (la.defined() && lb.defined()) {
            return la + lb;
        } else {
            return Expr();
        }
    } else if (const Sub *sub = e.as<Sub>()) {
        Expr la = is_linear(sub->a, linear);
        Expr lb = is_linear(sub->b, linear);
        if (is_zero(lb)) {
            return la;
        } else if (la.defined() && lb.defined()) {
            return la - lb;
        } else {
            return Expr();
        }
    } else if (const Mul *mul = e.as<Mul>()) {
        Expr la = is_linear(mul->a, linear);
        Expr lb = is_linear(mul->b, linear);
        if (is_zero(la) && is_zero(lb)) {
            return la;
        } else if (is_zero(la) && lb.defined()) {
            return mul->a * lb;
        } else if (la.defined() && is_zero(lb)) {
            return la * mul->b;
        } else {
            return Expr();
        }
    } else if (const Div *div = e.as<Div>()) {
        Expr la = is_linear(div->a, linear);
        if (is_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Mod *mod = e.as<Mod>()) {
        Expr la = is_linear(mod->a, linear);
        if (is_zero(la)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Ramp *r = e.as<Ramp>()) {
        Expr la = is_linear(r->base, linear);
        Expr lb = is_linear(r->stride, linear);
        if (is_zero(lb)) {
            return la;
        } else {
            return Expr();
        }
    } else if (const Broadcast *b = e.as<Broadcast>()) {
        return is_linear(b->value, linear);
    } else {
        return Expr();
    }
}

// Replace indirect loads with dynamic_shuffle intrinsics where
// possible.
class FindDirectCopies : public IRMutator {
    using IRMutator::visit;

    struct LoopVar {
        std::string name;
        Expr min;
        Expr extent;
    };

    std::vector<LoopVar> loop_vars;
    std::set<std::string> loops_to_be_removed;

    Stmt visit(const For *op) override {
      // debug(0) << "FindDirectCopies::for " << op->name << "\n";
      loop_vars.push_back({op->name, op->min, op->extent});
      Stmt mutated = IRMutator::visit(op);
      loop_vars.pop_back();
      if (loops_to_be_removed.count(op->name) > 0) {
        loops_to_be_removed.erase(op->name);
        return mutated.as<For>()->body;
      }
      return mutated;
    }

    Stmt visit(const Store *op) override {
        // debug(0) << "[begin] FindDirectCopies::store\n";
        Expr value = op->value;//mutate(op->value);
        const Load* maybe_load = value.as<Load>();
        if (maybe_load) {
            // debug(0) << "FindDirectCopies::" << op->name << " " <<  maybe_load->name << "\n";
            // debug(0) << op->index << "\n";
            // debug(0) << maybe_load->index << "\n";
          // for (const auto& v: loop_vars) {
            const auto& v = loop_vars.back();
            Scope<Expr> local_scope;
            Expr var = Variable::make(op->index.type(), v.name);
            // local_scope.push(v.name, var);
            local_scope.push(v.name, 1);
            // debug(0) << "is_linear (stride): " << v.name << " " << is_linear(op->index, local_scope) << "\n";
            // debug(0) << "is_linear (stride): " << v.name << " " << is_linear(maybe_load->index, local_scope) << "\n";
            Expr op_index = mutate(op->index);
            Expr value_index = mutate(maybe_load->index);
            Expr store_stride = is_linear(op_index, local_scope);
            Expr value_stride = is_linear(value_index, local_scope);
            if (is_one(store_stride) && is_one(value_stride)) {
                loops_to_be_removed.insert(v.name);
                Expr store_base = substitute(var, v.min, op_index);
                store_base = simplify(store_base);
                Expr value_base = substitute(var, v.min, value_index);
                value_base = simplify(value_base);
                debug(0) << "is_linear (stride): " << v.name << " " << is_linear(op_index, local_scope) << "\n";
                debug(0) << "is_linear (stride): " << v.name << " " << is_linear(value_index, local_scope) << "\n";
                debug(0) << ">>> " << store_base << "\n>>> "
                          << value_base << "\n>>>" << v.extent << "\n";

                Expr copy_call = Call::make(Int(32), "halide_xtensa_copy_1d", {op->name, store_base, maybe_load->name, value_base, v.extent, op->value.type().bytes()}, Call::PureExtern);
                // Expr var_copy = Variable::make(copy_call.type(), op->name + "copy_id");
                // Stmt was_copy_scheduled = AssertStmt::make(var_copy > 0, -1);
                // Stmt copy_let = LetStmt::make(op->name + "copy_id", copy_call, was_copy_scheduled);

                Expr wait_result = Call::make(Int(32), "halide_wait_for_copy", {copy_call}, Call::PureExtern);
                Stmt wait_is_done = AssertStmt::make(wait_result == 0, -1);

                return wait_is_done;
                // return Block::make(copy_let, wait_is_done);
            }
         // }
        }
        return IRMutator::visit(op);
    }

public:
    FindDirectCopies() { }
};

Stmt match_xtensa_patterns(Stmt s) {
    s = OptimizeShuffles(64).mutate(s);
    // s = FindDirectCopies().mutate(s);

    s = align_loads(s, 64);
    // s = common_subexpression_elimination(s);
    // Don't simplify here, otherwise it will re-collapse the loads we
    // want to carry across loop iterations.

    // Use at most 16 vector registers for carrying values.
    // s = loop_carry(s, 16);
    //     s = simplify(s);
    //     s = substitute_in_all_lets(s);
    for (int ix = 0; ix < 10; ix++) {
        s = MatchXtensaPatterns().mutate(s);
    }
    // Split to the native vectors sizes.
    s = substitute_in_all_lets(s);
    s = SplitVectorsToNativeSizes().mutate(s);
    s = SimplifySliceConcat().mutate(s);
    // Extra run to replace cast + concat, etc.
    s = MatchXtensaPatterns().mutate(s);
    // s = simplify(common_subexpression_elimination(s));
    s = common_subexpression_elimination(s);

    return s;
}

}  // namespace Internal
}  // namespace Halide

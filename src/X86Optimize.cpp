#include "X86Optimize.h"

#include "CSE.h"
// FIXME: move lower_int_uint_div out of CodeGen_Internal to remove this dependency.
#include "CodeGen_Internal.h"
#include "FindIntrinsics.h"
#include "IR.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

// Populate feature flags in a target according to those implied by
// existing flags, so that instruction patterns can just check for the
// oldest feature flag that supports an instruction.
Target complete_x86_target(Target t) {
    if (t.has_feature(Target::AVX512_SapphireRapids)) {
        t.set_feature(Target::AVX512_Cannonlake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake)) {
        t.set_feature(Target::AVX512_Skylake);
    }
    if (t.has_feature(Target::AVX512_Cannonlake) ||
        t.has_feature(Target::AVX512_Skylake) ||
        t.has_feature(Target::AVX512_KNL)) {
        t.set_feature(Target::AVX2);
    }
    if (t.has_feature(Target::AVX2)) {
        t.set_feature(Target::AVX);
    }
    if (t.has_feature(Target::AVX)) {
        t.set_feature(Target::SSE41);
    }
    return t;
}

#if defined(WITH_X86)

namespace {

// i32(i16_a)*i32(i16_b) +/- i32(i16_c)*i32(i16_d) can be done by
// interleaving a, c, and b, d, and then using dot_product.
bool should_use_dot_product(const Expr &a, const Expr &b, std::vector<Expr> &result) {
    Type t = a.type();
    internal_assert(b.type() == t) << a << " and " << b << " don't match types\n";

    if (!(t.is_int() && t.bits() == 32 && t.lanes() >= 4)) {
        return false;
    }

    const Call *ma = Call::as_intrinsic(a, {Call::widening_mul});
    const Call *mb = Call::as_intrinsic(b, {Call::widening_mul});
    // dot_product can't handle mixed type widening muls.
    if (ma && ma->args[0].type() != ma->args[1].type()) {
        return false;
    }
    if (mb && mb->args[0].type() != mb->args[1].type()) {
        return false;
    }
    // If the operands are widening shifts, we might be able to treat these as
    // multiplies.
    const Call *sa = Call::as_intrinsic(a, {Call::widening_shift_left});
    const Call *sb = Call::as_intrinsic(b, {Call::widening_shift_left});
    if (sa && !is_const(sa->args[1])) {
        sa = nullptr;
    }
    if (sb && !is_const(sb->args[1])) {
        sb = nullptr;
    }
    if ((ma || sa) && (mb || sb)) {
        Expr a0 = ma ? ma->args[0] : sa->args[0];
        Expr a1 = ma ? ma->args[1] : lossless_cast(sa->args[0].type(), simplify(make_const(sa->type, 1) << sa->args[1]));
        Expr b0 = mb ? mb->args[0] : sb->args[0];
        Expr b1 = mb ? mb->args[1] : lossless_cast(sb->args[0].type(), simplify(make_const(sb->type, 1) << sb->args[1]));
        if (a1.defined() && b1.defined()) {
            std::vector<Expr> args = {a0, a1, b0, b1};
            result.swap(args);
            return true;
        }
    }
    return false;
}

/** A top-down code optimizer that replaces Halide IR with VectorIntrinsics specific to x86. */
class Optimize_X86 : public IRMutator {
public:
    /** Create an x86 code optimizer. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    Optimize_X86(const Target &t)
        : target(t) {
    }

protected:
    bool should_peephole_optimize(const Type &type) {
        // We only have peephole optimizations for vectors here.
        // FIXME: should we only optimize vectors that are multiples of the native vector width?
        //        when we do, we fail simd_op_check tests on weird vector sizes.
        return type.is_vector();
    }

    Expr visit(const Div *op) override {
        if (!should_peephole_optimize(op->type) || !op->type.is_int_or_uint()) {
            return IRMutator::visit(op);
        }
        // Lower division here in order to do pattern-matching on intrinsics.
        return mutate(lower_int_uint_div(op->a, op->b));
    }

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    Expr visit(const Add *op) override {
        if (!should_peephole_optimize(op->type)) {
            return IRMutator::visit(op);
        }

        std::vector<Expr> matches;
        // TODO(rootjalex): is it possible to rewrite should_use_dot_product
        // as a series of rewrite-rules? lossless_cast is the hardest part.
        const int lanes = op->type.lanes();

        // FIXME: should we check for accumulating dot_products first?
        //        can there even be overlap between these?
        auto rewrite = IRMatcher::rewriter(IRMatcher::add(op->a, op->b), op->type);
        if (
            // Only AVX512_SapphireRapids has accumulating dot products.
            target.has_feature(Target::AVX512_SapphireRapids) &&
            // FIXME: add the float16 -> float32 versions as well.
            (op->type.element_of() == Int(32)) &&

            // Accumulating pmaddubsw
            (rewrite(
                 x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                 v_intrin("dot_product", x, y, z),
                 is_uint(y, 8) && is_int(z, 8)) ||

             rewrite(
                 x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                 v_intrin("dot_product", x, z, y),
                 is_int(y, 8) && is_uint(z, 8)) ||

             rewrite(
                 h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes) + z,
                 v_intrin("dot_product", z, x, y),
                 is_uint(x, 8) && is_int(y, 8)) ||

             rewrite(
                 h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes) + z,
                 v_intrin("dot_product", z, y, x),
                 is_int(x, 8) && is_uint(y, 8)) ||

             // Accumulating pmaddwd.
             rewrite(
                 x + h_add(widening_mul(y, z), lanes),
                 v_intrin("dot_product", x, y, z),
                 is_int(y, 16, lanes * 2) && is_int(z, 16, lanes * 2)) ||

             rewrite(
                 h_add(widening_mul(x, y), lanes) + z,
                 v_intrin("dot_product", z, x, y),
                 is_int(x, 16, lanes * 2) && is_int(y, 16, lanes * 2)) ||

             false)) {
            return mutate(rewrite.result);
        }

        if ((op->type.lanes() % 4 == 0) && should_use_dot_product(op->a, op->b, matches)) {
            Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
            Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
            // We have dot_products for every x86 arch (because SSE2 has it),
            // so this is `always` safe (as long as the output type lanes has
            // a factor of 4).
            return mutate(VectorIntrinsic::make(op->type, "dot_product", {ac, bd}));
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (!should_peephole_optimize(op->type)) {
            return IRMutator::visit(op);
        }

        std::vector<Expr> matches;
        // TODO(rootjalex): same issue as the Add case, lossless_cast and
        // lossless_negate are hard to use in rewrite rules.

        if ((op->type.lanes() % 4 == 0) && should_use_dot_product(op->a, op->b, matches)) {
            // Negate one of the factors in the second expression
            Expr negative_2 = lossless_negate(matches[2]);
            Expr negative_3 = lossless_negate(matches[3]);
            if (negative_2.defined() || negative_3.defined()) {
                if (negative_2.defined()) {
                    matches[2] = negative_2;
                } else {
                    matches[3] = negative_3;
                }
                Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
                Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
                // Always safe, see comment in Add case above.
                return mutate(VectorIntrinsic::make(op->type, "dot_product", {ac, bd}));
            }
        }

        return IRMutator::visit(op);
    }

    Expr visit(const Cast *op) override {
        if (!should_peephole_optimize(op->type)) {
            return IRMutator::visit(op);
        }

        const int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::cast(op->type, op->value), op->type);

        // TODO: saturating casts should be intrinsics, and supported in IRMatch.h.
        const Expr i32_i16max = cast(Int(32, lanes), Int(16).max());
        const Expr i32_i16min = cast(Int(32, lanes), Int(16).min());
        const Expr i16_i8max = cast(Int(16, lanes), Int(8).max());
        const Expr i16_i8min = cast(Int(16, lanes), Int(8).min());
        const Expr i16_u8max = cast(Int(16, lanes), UInt(8).max());
        const Expr i16_u8min = cast(Int(16, lanes), UInt(8).min());
        const Expr i32_u16max = cast(Int(32, lanes), UInt(16).max());
        const Expr i32_u16min = cast(Int(32, lanes), UInt(16).min());

        if (
            // pmulhrs is supported via AVX2 and SSE41, so SSE41 is the LCD.
            (target.has_feature(Target::SSE41) &&
             rewrite(
                 cast(Int(16, lanes), rounding_shift_right(widening_mul(x, y), 15)),
                 v_intrin("pmulhrs", x, y),
                 is_int(x, 16) && is_int(y, 16))) ||

            // saturating_narrow is always supported (via SSE2) for:
            //   int32 -> int16, int16 -> int8, int16 -> uint8
            rewrite(
                cast(Int(16, lanes), max(min(x, i32_i16min), i32_i16min)),
                v_intrin("saturating_narrow", x),
                is_int(x, 32)) ||

            rewrite(
                cast(Int(8, lanes), max(min(x, i16_i8min), i16_i8min)),
                v_intrin("saturating_narrow", x),
                is_int(x, 16)) ||

            rewrite(
                cast(UInt(8, lanes), max(min(x, i16_u8min), i16_u8min)),
                v_intrin("saturating_narrow", x),
                is_int(x, 16)) ||

            //   int32 -> uint16 is supported via SSE41
            (target.has_feature(Target::SSE41) &&
             rewrite(
                 cast(UInt(16, lanes), max(min(x, i32_u16min), i32_u16min)),
                 v_intrin("saturating_narrow", x),
                 is_int(x, 32))) ||

            // f32_to_bf16 is supported only via Target::AVX512_SapphireRapids
            (target.has_feature(Target::AVX512_SapphireRapids) &&
             rewrite(
                 cast(BFloat(16, lanes), x),
                 v_intrin("f32_to_bf16", x),
                 is_float(x, 32))) ||

            false) {
            return mutate(rewrite.result);
        }

        // TODO: should we handle CodeGen_X86's weird 8 -> 16 bit issue here?

        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!should_peephole_optimize(op->type)) {
            return IRMutator::visit(op);
        }

        // TODO: This optimization is hard to do via a rewrite-rule because of lossless_cast.

        // A 16-bit mul-shift-right of less than 16 can sometimes be rounded up to a
        // full 16 to use pmulh(u)w by left-shifting one of the operands. This is
        // handled here instead of in the lowering of mul_shift_right because it's
        // unlikely to be a good idea on platforms other than x86, as it adds an
        // extra shift in the fully-lowered case.
        if ((op->type.element_of() == UInt(16) ||
             op->type.element_of() == Int(16)) &&
            op->is_intrinsic(Call::mul_shift_right)) {
            internal_assert(op->args.size() == 3);
            const uint64_t *shift = as_const_uint(op->args[2]);
            if (shift && *shift < 16 && *shift >= 8) {
                Type narrow = op->type.with_bits(8);
                Expr narrow_a = lossless_cast(narrow, op->args[0]);
                Expr narrow_b = narrow_a.defined() ? Expr() : lossless_cast(narrow, op->args[1]);
                int shift_left = 16 - (int)(*shift);
                if (narrow_a.defined()) {
                    return mutate(mul_shift_right(op->args[0] << shift_left, op->args[1], 16));
                } else if (narrow_b.defined()) {
                    return mutate(mul_shift_right(op->args[0], op->args[1] << shift_left, 16));
                }
            }
        }

        const int lanes = op->type.lanes();
        const int bits = op->type.bits();

        auto rewrite = IRMatcher::rewriter(op, op->type);

        Type unsigned_type = op->type.with_code(halide_type_uint);
        auto x_uint = cast(unsigned_type, x);
        auto y_uint = cast(unsigned_type, y);

        if (
            // We can redirect signed rounding halving add to unsigned rounding
            // halving add by adding 128 / 32768 to the result if the sign of the
            // args differs.
            ((op->type.is_int() && bits <= 16) &&
             rewrite(
                 rounding_halving_add(x, y),
                 cast(op->type, rounding_halving_add(x_uint, y_uint) +
                                    ((x_uint ^ y_uint) & (1 << (bits - 1)))))) ||

            // On x86, there are many 3-instruction sequences to compute absd of
            // unsigned integers. This one consists solely of instructions with
            // throughput of 3 ops per cycle on Cannon Lake.
            //
            // Solution due to Wojciech Mula:
            // http://0x80.pl/notesen/2018-03-11-sse-abs-unsigned.html
            (op->type.is_uint() &&
             rewrite(
                 absd(x, y),
                 saturating_sub(x, y) | saturating_sub(y, x))) ||

            // Current best way to lower absd on x86.
            (op->type.is_int() &&
             rewrite(
                 absd(x, y),
                 max(x, y) - min(x, y))) ||

            // pmulh is always supported (via SSE2).
            ((op->type.is_int_or_uint() && bits == 16) &&
             rewrite(
                 mul_shift_right(x, y, 16),
                 v_intrin("pmulh", x, y))) ||

            // saturating_pmulhrs is supported via SSE41
            ((target.has_feature(Target::SSE41) &&
              op->type.is_int() && bits == 16) &&
             rewrite(
                 rounding_mul_shift_right(x, y, 15),
                 v_intrin("saturating_pmulhrs", x, y))) ||

            // TODO(rootjalex): The following intrinsics are
            // simply one-to-one mappings, should they even
            // be handled here?

            // int(8 | 16 | 32) -> uint is supported via SSE41
            // float32 is always supported (via SSE2).
            (((target.has_feature(Target::SSE41) && op->type.is_int() && bits <= 32) ||
              (op->type.is_float() && bits == 32)) &&
             rewrite(
                 abs(x),
                 v_intrin("abs", x))) ||

            // saturating ops for 8 and 16 bits are always supported (via SSE2).
            ((bits == 8 || bits == 16) &&
             (rewrite(
                  saturating_add(x, y),
                  v_intrin("saturating_add", x, y)) ||
              rewrite(
                  saturating_sub(x, y),
                  v_intrin("saturating_sub", x, y)))) ||

            // pavg ops for 8 and 16 bits are always supported (via SSE2).
            ((op->type.is_uint() && (bits == 8 || bits == 16)) &&
             rewrite(
                 rounding_halving_add(x, y),
                 v_intrin("rounding_halving_add", x, y))) ||

            // int16 -> int32 widening_mul has a (v)pmaddwd implementation.
            // always supported (via SSE2).
            ((op->type.is_int() && (bits == 32)) &&
             rewrite(
                 widening_mul(x, y),
                 v_intrin("widening_mul", x, y),
                 is_int(x, 16) && is_int(y, 16))) ||

            (target.has_feature(Target::AVX512_SapphireRapids) &&
             (op->type.is_int() && (bits == 32)) &&
             // SapphireRapids accumulating dot products.
             (rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes)),
                  v_intrin("saturating_dot_product", x, y, z),
                  is_uint(y, 8) && is_int(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes)),
                  v_intrin("saturating_dot_product", x, z, y),
                  is_int(y, 8) && is_uint(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 2), widening_mul(y, z)), lanes)),
                  v_intrin("saturating_dot_product", x, y, z),
                  is_uint(y, 8) && is_int(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 2), widening_mul(y, z)), lanes)),
                  v_intrin("saturating_dot_product", x, z, y),
                  is_int(y, 8) && is_uint(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(widening_mul(y, z), lanes)),
                  v_intrin("saturating_dot_product", x, z, y),
                  is_int(y, 16, lanes * 2) && is_int(z, 16, lanes * 2)) ||

              false)) ||

            false) {
            return mutate(rewrite.result);
        }

        // Fixed-point intrinsics should be lowered here.
        // This is safe because this mutator is top-down.
        if (op->is_intrinsic({
                Call::halving_add,
                Call::halving_sub,
                Call::mul_shift_right,
                Call::rounding_halving_add,
                Call::rounding_mul_shift_right,
                Call::rounding_shift_left,
                Call::rounding_shift_right,
                Call::saturating_add,
                Call::saturating_sub,
                Call::sorted_avg,
                Call::widening_add,
                Call::widening_mul,
                Call::widening_shift_left,
                Call::widening_shift_right,
                Call::widening_sub,
            })) {
            // TODO: Should we have a base-class that does this + the VectorReduce lowering needed below?
            return mutate(lower_intrinsic(op));
        }

        return IRMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        // FIXME: We need to split up VectorReduce nodes in the same way that
        //        CodeGen_LLVM::codegen_vector_reduce does, in order to do all
        //        matching here.
        if ((op->op != VectorReduce::Add && op->op != VectorReduce::SaturatingAdd) ||
            !should_peephole_optimize(op->type)) {
            return IRMutator::visit(op);
        }

        const int lanes = op->type.lanes();
        const int value_lanes = op->value.type().lanes();
        const int factor = value_lanes / lanes;
        Expr value = op->value;

        switch (op->op) {
        case VectorReduce::Add: {
            auto rewrite = IRMatcher::rewriter(IRMatcher::h_add(value, lanes), op->type);
            auto x_is_int_or_uint = is_int(x) || is_uint(x);
            auto y_is_int_or_uint = is_int(y) || is_uint(y);
            if (
                // 2-way dot-products, int16 -> int32 is always supported (via SSE2).
                ((factor == 2) &&
                 (rewrite(
                      h_add(cast(Int(32, value_lanes), widening_mul(x, y)), lanes),
                      v_intrin("dot_product", cast(Int(16, value_lanes), x), cast(Int(16, value_lanes), y)),
                      x_is_int_or_uint && y_is_int_or_uint) ||

                  // Horizontal widening add via pmaddwd
                  rewrite(
                      h_add(cast(Int(32, value_lanes), x), lanes),
                      v_intrin("dot_product", x, make_const(Int(16, value_lanes), 1)),
                      is_int(x, 16)) ||

                  (rewrite(
                      h_add(widening_mul(x, y), lanes),
                      v_intrin("dot_product", x, y),
                      is_int(x, 16) && is_int(y, 16))) ||

                  // pmaddub supported via SSE41
                  (target.has_feature(Target::SSE41) &&
                   // Horizontal widening adds using 2-way saturating dot products.
                   (rewrite(
                        h_add(cast(UInt(16, value_lanes), x), lanes),
                        cast(UInt(16, lanes), typed(Int(16, lanes), v_intrin("saturating_dot_product", x, make_const(Int(8, value_lanes), 1)))),
                        is_uint(x, 8)) ||

                    rewrite(
                        h_add(cast(Int(16, value_lanes), x), lanes),
                        v_intrin("saturating_dot_product", x, make_const(Int(8, value_lanes), 1)),
                        is_uint(x, 8)) ||

                    rewrite(
                        h_add(cast(Int(16, value_lanes), x), lanes),
                        v_intrin("saturating_dot_product", make_const(UInt(8, value_lanes), 1), x),
                        is_int(x, 8)) ||

                    // SSE41 and AVX2 support horizontal_add via phadd intrinsics.
                    rewrite(
                        h_add(x, lanes),
                        v_intrin("horizontal_add", x),
                        is_int(x, 16, lanes * 2) || is_uint(x, 16, lanes * 2) ||
                            is_int(x, 32, lanes * 2) || is_uint(x, 32, lanes * 2)) ||

                    // TODO: add in Andrew's psadbw pattern.

                    false)) ||

                  false))) {
                return mutate(rewrite.result);
            }
            break;
        }
        case VectorReduce::SaturatingAdd: {
            auto rewrite = IRMatcher::rewriter(IRMatcher::h_satadd(value, lanes), op->type);
            if (
                // Saturating dot products are supported via SSE41 and AVX2.
                ((factor == 2) && target.has_feature(Target::SSE41) &&
                 (rewrite(
                      h_satadd(widening_mul(x, y), lanes),
                      v_intrin("saturating_dot_product", x, y),
                      is_uint(x, 8) && is_int(y, 8)) ||

                  rewrite(
                      h_satadd(widening_mul(x, y), lanes),
                      v_intrin("saturating_dot_product", y, x),
                      is_int(x, 8) && is_uint(y, 8)) ||

                  false))) {
                return mutate(rewrite.result);
            }
            break;
        }
        default:
            break;
        }

        // FIXME: We need to split up VectorReduce nodes in the same way that
        //        CodeGen_LLVM::codegen_vector_reduce does, in order to do all
        //        matching here.

        return IRMutator::visit(op);
    }

private:
    const Target &target;

    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
};

}  // namespace

Stmt optimize_x86_instructions(Stmt s, const Target &t) {
    s = Optimize_X86(complete_x86_target(t)).mutate(s);
    // Some of the rules above can introduce repeated sub-terms, so run CSE again.
    s = common_subexpression_elimination(s);
    return s;
}

#else  // WITH_X86

Stmt optimize_x86_instructions(Stmt s, const Target &t) {
    user_error << "x86 not enabled for this build of Halide.\n";
    return Stmt();
}

#endif  // WITH_X86

}  // namespace Internal
}  // namespace Halide

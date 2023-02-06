#include "X86Optimize.h"

#include "CSE.h"
#include "FindIntrinsics.h"
#include "IR.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "InstructionSelector.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

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

/** A top-down code optimizer that replaces Halide IR with VectorInstructions specific to x86. */
class Optimize_X86 : public InstructionSelector {
public:
    /** Create an x86 code optimizer. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    Optimize_X86(const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb)
        : InstructionSelector(target, codegen, fvb) {
    }

protected:
    bool should_peephole_optimize(const Type &type) {
        // We only have peephole optimizations for vectors here.
        // FIXME: should we only optimize vectors that are multiples of the native vector width?
        //        when we do, we fail simd_op_check tests on weird vector sizes.
        return type.is_vector();
    }

    using InstructionSelector::visit;

    /** Nodes for which we want to emit specific sse/avx intrinsics */
    Expr visit(const Add *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
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
            ((op->type.element_of() == Int(32)) ||
             (op->type.element_of() == Float(32))) &&

            // Accumulating pmaddubsw
            (rewrite(
                 x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                 v_instr(VectorInstruction::dot_product, x, y, z),
                 is_uint(y, 8) && is_int(z, 8)) ||

             rewrite(
                 x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                 v_instr(VectorInstruction::dot_product, x, z, y),
                 is_int(y, 8) && is_uint(z, 8)) ||

             rewrite(
                 h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes) + z,
                 v_instr(VectorInstruction::dot_product, z, x, y),
                 is_uint(x, 8) && is_int(y, 8)) ||

             rewrite(
                 h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes) + z,
                 v_instr(VectorInstruction::dot_product, z, y, x),
                 is_int(x, 8) && is_uint(y, 8)) ||

             // Accumulating pmaddwd.
             rewrite(
                 x + h_add(widening_mul(y, z), lanes),
                 v_instr(VectorInstruction::dot_product, x, y, z),
                 is_int(y, 16, lanes * 2) && is_int(z, 16, lanes * 2)) ||

             rewrite(
                 h_add(widening_mul(x, y), lanes) + z,
                 v_instr(VectorInstruction::dot_product, z, x, y),
                 is_int(x, 16, lanes * 2) && is_int(y, 16, lanes * 2)) ||

             // Accumulating fp dot products.
             // TODO(rootjalex): This would be more powerful with lossless_cast checking.
             rewrite(
                 x + h_add(cast(Float(32, lanes * 4), y) * cast(Float(32, lanes * 4), z), lanes),
                 v_instr(VectorInstruction::dot_product, x, y, z),
                 is_bfloat(y, 16) && is_bfloat(z, 16)) ||

             rewrite(
                 h_add(cast(Float(32, lanes * 4), x) * cast(Float(32, lanes * 4), y), lanes) + z,
                 v_instr(VectorInstruction::dot_product, z, x, y),
                 is_bfloat(x, 16) && is_bfloat(y, 16)) ||

             false)) {
            return mutate(rewrite.result);
            // } else if (
            //     rewrite(
            //         widening_mul(x, y) + (z + widening_mul(w, u)),
            //         (widening_mul(x, y) + widening_mul(w, u)) + z,
            //         !is_intrin(z, Call::widening_mul) &&
            //         is_int(x, 16) && is_int(y, 16) &&
            //         is_int(w, 16) && is_int(u, 16)) ||

            //     rewrite(
            //         widening_mul(x, y) + (widening_mul(w, u) + z),
            //         (widening_mul(x, y) + widening_mul(w, u)) + z,
            //         !is_intrin(z, Call::widening_mul) &&
            //         is_int(x, 16) && is_int(y, 16) &&
            //         is_int(w, 16) && is_int(u, 16)) ||

            //     false) {
            //     return mutate(rewrite.result);
        }

        if ((op->type.lanes() % 4 == 0) && should_use_dot_product(op->a, op->b, matches)) {
            Expr ac = Shuffle::make_interleave({matches[0], matches[2]});
            Expr bd = Shuffle::make_interleave({matches[1], matches[3]});
            // We have dot_products for every x86 arch (because SSE2 has it),
            // so this is `always` safe (as long as the output type lanes has
            // a factor of 4).
            return mutate(VectorInstruction::make(op->type, VectorInstruction::dot_product, {ac, bd}));
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
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
                return mutate(VectorInstruction::make(op->type, VectorInstruction::dot_product, {ac, bd}));
            }
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const Cast *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        const int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::cast(op->type, op->value), op->type);

        if (
            // pmulhrs is supported via AVX2 and SSE41, so SSE41 is the LCD.
            (target.has_feature(Target::SSE41) &&
             rewrite(
                 cast(Int(16, lanes), rounding_shift_right(widening_mul(x, y), 15)),
                 v_instr(VectorInstruction::pmulhrs, x, y),
                 is_int(x, 16) && is_int(y, 16))) ||

            // f32_to_bf16 is supported only via Target::AVX512_SapphireRapids
            (target.has_feature(Target::AVX512_SapphireRapids) &&
             rewrite(
                 cast(BFloat(16, lanes), x),
                 v_instr(VectorInstruction::f32_to_bf16, x),
                 is_float(x, 32))) ||

            (target.has_feature(Target::SSE41) &&
             (rewrite(
                 cast(Int(32, lanes), widening_mul(x, y)),
                 v_instr(VectorInstruction::dot_product, reinterpret(Int(16, lanes * 2), cast(Int(32, lanes), x)),
                         reinterpret(Int(16, lanes * 2), cast(Int(32, lanes), y))),
                 is_uint(x, 8) && is_uint(y, 8)))) ||

            // TODO: check for cases of using saturating cast safely.

            // saturating_narrow is always supported (via SSE2) for:
            //   int32 -> int16, int16 -> int8, int16 -> uint8
            rewrite(
                cast(Int(16, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 32) &&
                    upper_bounded(x, (int64_t)std::numeric_limits<int16_t>::max(), this) &&
                    lower_bounded(x, (int64_t)std::numeric_limits<int16_t>::min(), this)) ||

            rewrite(
                cast(Int(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16) &&
                    upper_bounded(x, (int64_t)std::numeric_limits<int8_t>::max(), this) &&
                    lower_bounded(x, (int64_t)std::numeric_limits<int8_t>::min(), this)) ||

            rewrite(
                cast(UInt(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16) &&
                    upper_bounded(x, (int64_t)std::numeric_limits<uint8_t>::max(), this) &&
                    lower_bounded(x, (int64_t)std::numeric_limits<uint8_t>::min(), this)) ||

            // i32 -> u16 is supported via SSE41
            (target.has_feature(Target::SSE41) &&
             (rewrite(
                  cast(UInt(16, lanes), x),
                  v_instr(VectorInstruction::saturating_narrow, x),
                  is_int(x, 32) &&
                      upper_bounded(x, (int64_t)std::numeric_limits<uint16_t>::max(), this) &&
                      lower_bounded(x, (int64_t)std::numeric_limits<uint16_t>::min(), this)) ||

              false)) ||

            false) {
            return mutate(rewrite.result);
        }

        // TODO: should we handle CodeGen_X86's weird 8 -> 16 bit issue here?
        if (const Call *mul = Call::as_intrinsic(op->value, {Call::widening_mul})) {
            if (op->value.type().bits() < op->type.bits() && op->type.bits() <= 32) {
                // LLVM/x86 really doesn't like 8 -> 16 bit multiplication. If we're
                // widening to 32-bits after a widening multiply, LLVM prefers to see a
                // widening multiply directly to 32-bits. This may result in extra
                // casts, so simplify to remove them.
                return mutate(simplify(Mul::make(Cast::make(op->type, mul->args[0]), Cast::make(op->type, mul->args[1]))));
            }
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        // TODO(rootjalex): This optimization is hard to do via a rewrite-rule because of lossless_cast.

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
        using IRMatcher::typed;

        Type unsigned_type = op->type.with_code(halide_type_uint);
        // Type signed_type = op->type.with_code(halide_type_int);
        // Type unsigned_narrow_type = (bits > 8) ? unsigned_type.narrow() : unsigned_type;
        // Type signed_narrow_type = (bits > 8) ? signed_type.narrow() : signed_type;
        auto x_uint = cast(unsigned_type, x);
        auto y_uint = cast(unsigned_type, y);

        // auto x_zext_u8 = reinterpret(unsigned_narrow_type.with_lanes(lanes * 2), cast(unsigned_type, x));
        // auto y_zext_i8 = reinterpret(signed_narrow_type.with_lanes(lanes * 2), cast(unsigned_type, reinterpret(unsigned_narrow_type, y)));
        // auto c0_wide_i8 = reinterpret(signed_narrow_type.with_lanes(lanes * 2), fold(IRMatcher::broadcast(IRMatcher::as_scalar(c0), lanes * 2)));
        // auto c0_i8 = cast(Int(8).with_lanes(lanes), c0);

        if (
            // saturating_narrow is always supported (via SSE2) for:
            //   int32 -> int16, int16 -> int8, int16 -> uint8
            rewrite(
                saturating_cast(Int(16, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 32)) ||

            rewrite(
                saturating_cast(Int(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16)) ||

            rewrite(
                saturating_cast(UInt(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16)) ||

            // u16 -> u8 can be done if the MSB is 0.
            rewrite(
                saturating_cast(UInt(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, reinterpret(Int(16, lanes), x)),
                is_uint(x, 16) && upper_bounded(x, (int64_t)std::numeric_limits<int16_t>::max(), this)) ||

            // u16 -> i8 can be done if MSB is 0.
            rewrite(
                saturating_cast(Int(8, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, reinterpret(Int(16, lanes), x)),
                is_uint(x, 16) && upper_bounded(x, (int64_t)std::numeric_limits<int16_t>::max(), this)) ||

            // u32 -> i16 can be done if the MSB is 0.
            rewrite(
                saturating_cast(Int(16, lanes), x),
                v_instr(VectorInstruction::saturating_narrow, reinterpret(Int(32, lanes), x)),
                is_uint(x, 32) && upper_bounded(x, (int64_t)std::numeric_limits<int32_t>::max(), this)) ||

            // TODO: is it worth doing u32 -> u16 this way?
            // i32 -> u16 is supported via SSE41
            (target.has_feature(Target::SSE41) &&
             (rewrite(
                  saturating_cast(UInt(16, lanes), x),
                  v_instr(VectorInstruction::saturating_narrow, x),
                  is_int(x, 32)) ||

              //   // Can also do faster widening_mul(u8, i8) on x86 via 2 pmovzxbw and pmaddubsw
              //   rewrite(widening_mul(x, y),
              //           v_instr(VectorInstruction::widening_mul, x, y),
              //         //   v_instr(VectorInstruction::saturating_dot_product, x_zext_u8, y_zext_i8),
              //           is_uint(x, 8) && is_int(y, 8)) ||

              //   rewrite(widening_mul(x, c0),
              //           reinterpret(op->type, typed(signed_type, v_instr(VectorInstruction::widening_mul, x, c0_i8))),
              //         //   reinterpret(op->type, typed(signed_type, v_instr(VectorInstruction::saturating_dot_product, x_zext_u8, c0_wide_i8))),
              //           is_uint(x, 8) && is_uint(c0, 8) && c0 <= 127) ||

              //   rewrite(widening_mul(c0, x),
              //           reinterpret(op->type, typed(signed_type, v_instr(VectorInstruction::widening_mul, x, c0_i8))),
              //         //   reinterpret(op->type, typed(signed_type, v_instr(VectorInstruction::saturating_dot_product, x_zext_u8, c0_wide_i8))),
              //           is_uint(x, 8) && is_uint(c0, 8) && c0 <= 127) ||

              false)) ||

            // Rewrite double saturating casts for supported types.
            // int32 -> uint8 and int32 -> int8 are always possible.
            rewrite(
                saturating_cast(Int(8, lanes), x),
                saturating_cast(Int(8, lanes), saturating_cast(Int(16, lanes), x)),
                is_int(x, 32)) ||

            rewrite(
                saturating_cast(UInt(8, lanes), x),
                saturating_cast(UInt(8, lanes), saturating_cast(Int(16, lanes), x)),
                is_int(x, 32)) ||

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
            rewrite(
                absd(x, y),
                saturating_sub(x, y) | saturating_sub(y, x),
                is_uint(x) && is_uint(y)) ||

            // Current best way to lower absd on x86.
            rewrite(
                absd(x, y),
                max(x, y) - min(x, y),
                is_int(x) && is_int(y)) ||

            // pmulh is always supported (via SSE2).
            ((op->type.is_int_or_uint() && bits == 16) &&
             rewrite(
                 mul_shift_right(x, y, 16),
                 v_instr(VectorInstruction::pmulh, x, y))) ||

            // saturating_pmulhrs is supported via SSE41
            ((target.has_feature(Target::SSE41) &&
              op->type.is_int() && bits == 16) &&
             rewrite(
                 rounding_mul_shift_right(x, y, 15),
                 // saturating_pmulhrs
                 select((x == typed(Int(16, lanes), -32768)) && (y == typed(Int(16, lanes), -32768)),
                        typed(Int(16, lanes), 32767),
                        v_instr(VectorInstruction::pmulhrs, x, y)))) ||

            // int(8 | 16 | 32) -> uint is supported via SSE41
            // float32 is always supported (via SSE2).
            (((target.has_feature(Target::SSE41) && op->type.is_int() && bits <= 32) ||
              (op->type.is_float() && bits == 32)) &&
             rewrite(
                 abs(x),
                 v_instr(VectorInstruction::abs, x))) ||

            // saturating ops for 8 and 16 bits are always supported (via SSE2).
            ((bits == 8 || bits == 16) &&
             (rewrite(
                  saturating_add(x, y),
                  v_instr(VectorInstruction::saturating_add, x, y)) ||
              rewrite(
                  saturating_sub(x, y),
                  v_instr(VectorInstruction::saturating_sub, x, y)))) ||

            // pavg ops for 8 and 16 bits are always supported (via SSE2).
            ((op->type.is_uint() && (bits == 8 || bits == 16)) &&
             rewrite(
                 rounding_halving_add(x, y),
                 v_instr(VectorInstruction::rounding_halving_add, x, y))) ||

            // int16 -> int32 widening_mul has a (v)pmaddwd implementation.
            // always supported (via SSE2).
            ((op->type.is_int() && (bits == 32)) &&
             (rewrite(
                  widening_mul(x, cast(Int(16, lanes), y)),
                  v_instr(VectorInstruction::dot_product, reinterpret(Int(16, lanes * 2), cast(Int(32, lanes), x)),
                          reinterpret(Int(16, lanes * 2), cast(Int(32, lanes), y))),
                  is_int(x, 16) && is_uint(y, 8)) ||

              // don't do this if one or more operands is a load, it's faster to load wide + do pmulld
              // rewrite(
              //  widening_mul(x, y),
              //  cast(op->type, x) * cast(op->type, y),
              //  is_load(x) || is_load(y)) ||

              // rewrite(
              //  widening_mul(x, y),
              //  v_instr(VectorInstruction::widening_mul, x, y),
              //  is_int(x, 16) && is_int(y, 16))
              false)) ||

            (target.has_feature(Target::AVX512_SapphireRapids) &&
             (op->type.is_int() && (bits == 32)) &&
             // SapphireRapids accumulating dot products.
             (rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes)),
                  v_instr(VectorInstruction::saturating_dot_product, x, y, z),
                  is_uint(y, 8) && is_int(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes)),
                  v_instr(VectorInstruction::saturating_dot_product, x, z, y),
                  is_int(y, 8) && is_uint(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 2), widening_mul(y, z)), lanes)),
                  v_instr(VectorInstruction::saturating_dot_product, x, y, z),
                  is_uint(y, 8) && is_int(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(cast(Int(32, lanes * 2), widening_mul(y, z)), lanes)),
                  v_instr(VectorInstruction::saturating_dot_product, x, z, y),
                  is_int(y, 8) && is_uint(z, 8)) ||

              rewrite(
                  saturating_add(x, h_satadd(widening_mul(y, z), lanes)),
                  v_instr(VectorInstruction::saturating_dot_product, x, z, y),
                  is_int(y, 16, lanes * 2) && is_int(z, 16, lanes * 2)) ||

              false)) ||

            false) {
            // std::cerr << Expr(op) << " -> " << rewrite.result << "\n";
            return mutate(rewrite.result);
        }

        // Fixed-point intrinsics should be lowered here.
        // This is safe because this mutator is top-down.
        // FIXME: Should this be default behavior of the base InstructionSelector class?
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
            return mutate(lower_intrinsic(op));
        }

        return InstructionSelector::visit(op);
    }

    Expr break_up_reduction(const VectorReduce *op, const int32_t factor) {
        Expr equiv = VectorReduce::make(op->op, op->value, op->value.type().lanes() / factor);
        return VectorReduce::make(op->op, equiv, op->type.lanes());
    }

    Expr visit(const VectorReduce *op) override {
        // FIXME: We need to split up VectorReduce nodes in the same way that
        //        CodeGen_LLVM::codegen_vector_reduce does, in order to do all
        //        matching here.
        if ((op->op != VectorReduce::Add && op->op != VectorReduce::SaturatingAdd) ||
            op->type.is_bool()) {
            return InstructionSelector::visit(op);
        }

        const int lanes = op->type.lanes();
        const int value_lanes = op->value.type().lanes();
        const int factor = value_lanes / lanes;
        Expr value = op->value;

        // Useful constants for some of the below rules.
        const Expr one_i16 = make_one(Int(16, value_lanes));
        const Expr one_i8 = make_one(Int(8, value_lanes));
        const Expr one_u8 = make_one(UInt(8, value_lanes));
        const Expr zero_i32 = make_zero(Int(32, lanes));
        const Expr zero_f32 = make_zero(Float(32, lanes));

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
                      v_instr(VectorInstruction::dot_product, cast(Int(16, value_lanes), x), cast(Int(16, value_lanes), y)),
                      x_is_int_or_uint && y_is_int_or_uint) ||

                  // Horizontal widening add via pmaddwd
                  rewrite(
                      h_add(cast(Int(32, value_lanes), x), lanes),
                      v_instr(VectorInstruction::dot_product, x, one_i16),
                      is_int(x, 16)) ||

                  (rewrite(
                      h_add(widening_mul(x, y), lanes),
                      v_instr(VectorInstruction::dot_product, x, y),
                      is_int(x, 16) && is_int(y, 16))) ||

                  // pmaddub supported via SSE41
                  (target.has_feature(Target::SSE41) &&
                   // Horizontal widening adds using 2-way saturating dot products.
                   (rewrite(
                        h_add(cast(UInt(16, value_lanes), x), lanes),
                        cast(UInt(16, lanes), typed(Int(16, lanes), v_instr(VectorInstruction::saturating_dot_product, x, one_i8))),
                        is_uint(x, 8)) ||

                    rewrite(
                        h_add(cast(Int(16, value_lanes), x), lanes),
                        v_instr(VectorInstruction::saturating_dot_product, x, one_i8),
                        is_uint(x, 8)) ||

                    rewrite(
                        h_add(cast(Int(16, value_lanes), x), lanes),
                        v_instr(VectorInstruction::saturating_dot_product, one_u8, x),
                        is_int(x, 8)) ||

                    // SSE41 and AVX2 support horizontal_add via phadd intrinsics.
                    rewrite(
                        h_add(x, lanes),
                        v_instr(VectorInstruction::horizontal_add, x),
                        is_int(x, 16, lanes * 2) || is_uint(x, 16, lanes * 2) ||
                            is_int(x, 32, lanes * 2) || is_uint(x, 32, lanes * 2)) ||

                    false)) ||
                  false)) ||

                // We can use the AVX512_SapphireRapids accumulating dot products
                // on pure VectorReduce nodes with 0 as the accumulator.
                ((factor == 4) &&
                 target.has_feature(Target::AVX512_SapphireRapids) &&
                 ((op->type.element_of() == Int(32)) ||
                  (op->type.element_of() == Float(32))) &&

                 // Accumulating pmaddubsw
                 (rewrite(
                      h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes),
                      v_instr(VectorInstruction::dot_product, zero_i32, x, y),
                      is_uint(x, 8) && is_int(y, 8)) ||

                  rewrite(
                      h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes),
                      v_instr(VectorInstruction::dot_product, zero_i32, y, x),
                      is_int(x, 8) && is_uint(y, 8)) ||

                  // Accumulating pmaddwd.
                  rewrite(
                      h_add(widening_mul(x, y), lanes),
                      v_instr(VectorInstruction::dot_product, zero_i32, x, y),
                      is_int(x, 16, lanes * 2) && is_int(y, 16, lanes * 2)) ||

                  // Accumulating fp dot products.
                  // TODO(rootjalex): This would be more powerful with lossless_cast checking.
                  rewrite(
                      h_add(cast(Float(32, lanes * 4), x) * cast(Float(32, lanes * 4), y), lanes),
                      v_instr(VectorInstruction::dot_product, zero_f32, x, y),
                      is_bfloat(x, 16) && is_bfloat(y, 16)) ||

                  false)) ||

                // psadbw is always supported via SSE2.
                ((factor == 8) &&
                 (rewrite(
                      h_add(cast(UInt(64, value_lanes), absd(x, y)), lanes),
                      v_instr(VectorInstruction::sum_absd, x, y),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  // Rewrite non-native sum-of-absolute-difference variants to the native
                  // op. We support reducing to various types. We could consider supporting
                  // multiple reduction factors too, but in general we don't handle non-native
                  // reduction factors for VectorReduce nodes (yet?).
                  rewrite(
                      h_add(cast(UInt(16, value_lanes), absd(x, y)), lanes),
                      cast(UInt(16, lanes), typed(UInt(64, lanes), v_instr(VectorInstruction::sum_absd, x, y))),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  rewrite(
                      h_add(cast(UInt(32, value_lanes), absd(x, y)), lanes),
                      cast(UInt(32, lanes), typed(UInt(64, lanes), v_instr(VectorInstruction::sum_absd, x, y))),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  rewrite(
                      h_add(cast(Int(16, value_lanes), absd(x, y)), lanes),
                      cast(Int(16, lanes), typed(UInt(64, lanes), v_instr(VectorInstruction::sum_absd, x, y))),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  rewrite(
                      h_add(cast(Int(32, value_lanes), absd(x, y)), lanes),
                      cast(Int(32, lanes), typed(UInt(64, lanes), v_instr(VectorInstruction::sum_absd, x, y))),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  rewrite(
                      h_add(cast(Int(64, value_lanes), absd(x, y)), lanes),
                      cast(Int(64, lanes), typed(UInt(64, lanes), v_instr(VectorInstruction::sum_absd, x, y))),
                      is_uint(x, 8) && is_uint(y, 8)) ||

                  false))) {
                return mutate(rewrite.result);
            }

            // If we see a pattern we want but the factor is too large, split it up and mutate.
            {
                auto rewrite = IRMatcher::rewriter(value, op->type);

                if ((factor % 2 == 0) &&
                    (rewrite(widening_mul(x, y), widening_mul(x, y), is_int(x, 16) && is_int(y, 16)) ||
                     false)) {
                    return mutate(break_up_reduction(op, 2));
                }
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
                      v_instr(VectorInstruction::saturating_dot_product, x, y),
                      is_uint(x, 8) && is_int(y, 8)) ||

                  rewrite(
                      h_satadd(widening_mul(x, y), lanes),
                      v_instr(VectorInstruction::saturating_dot_product, y, x),
                      is_int(x, 8) && is_uint(y, 8)) ||

                  false))) {
                return mutate(rewrite.result);
            }
            break;
        }
        default:
            break;
        }

        return InstructionSelector::visit(op);
    }

private:
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
};

}  // namespace

Stmt optimize_x86_instructions(const Stmt &s, const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb) {
    if (get_env_variable("HL_DISABLE_HALIDE_LOWERING") == "1") {
        return s;
    }

    Stmt stmt = Optimize_X86(target, codegen, fvb).mutate(s);

    // Some of the rules above can introduce repeated sub-terms, so run CSE again.
    if (!stmt.same_as(s)) {
        stmt = common_subexpression_elimination(stmt);
        return stmt;
    } else {
        return s;
    }
}

#else  // WITH_X86

Stmt optimize_x86_instructions(const Stmt &s, const Target &t, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb) {
    user_error << "x86 not enabled for this build of Halide.\n";
    return Stmt();
}

#endif  // WITH_X86

}  // namespace Internal
}  // namespace Halide

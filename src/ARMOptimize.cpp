#include "ARMOptimize.h"

#include "CSE.h"
#include "FindIntrinsics.h"
#include "IR.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "InstructionSelector.h"
#include "Simplify.h"
#include "CodeGen_Internal.h"

namespace Halide {
namespace Internal {

#if defined(WITH_ARM)

namespace {

/** A top-down code optimizer that replaces Halide IR with VectorInstructions specific to ARM. */
class Optimize_ARM : public InstructionSelector {
public:
    /** Create an ARM code optimizer. Processor features can be
     * enabled using the appropriate flags in the target struct. */
    Optimize_ARM(const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb)
        : InstructionSelector(target, codegen, fvb) {
    }

protected:
    // NEON can be disabled for older processors.
    bool neon_intrinsics_disabled() const {
        return target.has_feature(Target::NoNEON);
    }

    bool target_arm32() const {
        return target.bits == 32;
    }

    // Some instructions are only enabled with FP16.
    bool has_fp16() const {
        return target.has_feature(Target::ARMFp16);
    }

    bool should_peephole_optimize(const Type &type) const {
        // We only have peephole optimizations for vectors here.
        // FIXME: should we only optimize vectors that are multiples of the native vector width?
        //        when we do, we fail simd_op_check tests on weird vector sizes.
        return type.is_vector() && !neon_intrinsics_disabled() && ((type.lanes() % 2) == 0);
    }

    static bool enable_synthesized_rules() {
        return get_env_variable("HL_ENABLE_RAKE_RULES") == "1";
    }

    using InstructionSelector::visit;

    Expr try_to_use_pwadd_acc(const VectorReduce *op, const Expr &b) const {
        if (!target_arm32()) {
            return Expr();  // Only available on arm32.
        }
        // This is hard to express as a pattern due to the use of lossless_cast.
        const int factor = op->value.type().lanes() / op->type.lanes();
        if (factor == 2) {
            Type narrow_type = op->type.narrow().with_lanes(op->value.type().lanes());
            Expr narrow = lossless_cast(narrow_type, op->value);
            if (!narrow.defined() && op->type.is_int()) {
                // We can also safely accumulate from a uint into a
                // wider int, because the addition uses at most one
                // extra bit.
                narrow = lossless_cast(narrow_type.with_code(Type::UInt), op->value);
            }
            if (narrow.defined() && op->type.is_int_or_uint()) {
                return VectorInstruction::make(op->type, VectorInstruction::pairwise_widening_add_accumulate, {b, narrow});
            }
        }
        return Expr();
    }

    Expr visit(const Add *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        const int lanes = op->type.lanes();
        auto rewrite = IRMatcher::rewriter(IRMatcher::add(op->a, op->b), op->type);

        const Expr i8_1 = make_const(Int(8, lanes * 4), 1);
        const Expr u8_1 = make_const(UInt(8, lanes * 4), 1);

        // Search for accumulating dot product instructions.
        if (target.has_feature(Target::ARMDotProd) &&
            (
                // SDOT
                rewrite(
                    x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_int(x, 32, lanes) && is_int(y, 8, lanes * 4) && is_int(z, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_int(x, 32, lanes) && is_int(y, 8, lanes * 4) && is_int(z, 8, lanes * 4)) ||
                // UDOT
                rewrite(
                    x + h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_int(x, 32, lanes) && is_uint(y, 8, lanes * 4) && is_uint(z, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(Int(32, lanes * 4), widening_mul(y, z)), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_int(x, 32, lanes) && is_uint(y, 8, lanes * 4) && is_uint(z, 8, lanes * 4)) ||
                rewrite(
                    x + h_add(cast(UInt(32, lanes * 4), widening_mul(y, z)), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_uint(x, 32, lanes) && is_uint(y, 8, lanes * 4) && is_uint(z, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(UInt(32, lanes * 4), widening_mul(y, z)), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, z),
                    is_uint(x, 32, lanes) && is_uint(y, 8, lanes * 4) && is_uint(z, 8, lanes * 4)) ||

                // TODO: SUDOT (see below).

                // A sum is the same as a dot product with a vector of ones, and this appears to
                // be a bit faster.
                // SDOT
                rewrite(
                    x + h_add(cast(Int(32, lanes * 4), y), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, i8_1),
                    is_int(x, 32, lanes) && is_int(y, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(Int(32, lanes * 4), y), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, i8_1),
                    is_int(x, 32, lanes) && is_int(y, 8, lanes * 4)) ||
                // UDOT
                rewrite(
                    x + h_add(cast(Int(32, lanes * 4), y), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, u8_1),
                    is_int(x, 32, lanes) && is_uint(y, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(Int(32, lanes * 4), y), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, u8_1),
                    is_int(x, 32, lanes) && is_uint(y, 8, lanes * 4)) ||
                rewrite(
                    x + h_add(cast(UInt(32, lanes * 4), y), lanes),
                    v_instr(VectorInstruction::dot_product, x, y, u8_1),
                    is_uint(x, 32, lanes) && is_uint(y, 8, lanes * 4)) ||
                rewrite(
                    h_add(cast(UInt(32, lanes * 4), y), lanes) + x,
                    v_instr(VectorInstruction::dot_product, x, y, u8_1),
                    is_uint(x, 32, lanes) && is_uint(y, 8, lanes * 4)) ||


                false)) {
            return mutate(rewrite.result);
        }

        const int bits = op->type.bits();
        Type narrow_type = op->type.narrow();
        Type flipped_type = op->type.is_int() ? op->type.with_code(halide_type_uint) : op->type.with_code(halide_type_int);
        Type flipped_narrow_type = flipped_type.narrow();
        Expr narrow_one = (bits > 8) ? make_one(narrow_type) : make_one(op->type);
        Expr flipped_narrow_one = (bits > 8) ? make_one(flipped_narrow_type) : make_one(op->type);

        const auto are_ywpr_uint8 = is_uint(y, 8) && is_uint(w, 8) && is_uint(p, 8) && is_uint(r, 8);
        const auto are_zuqs_uint8 = is_uint(z, 8) && is_uint(u, 8) && is_uint(q, 8) && is_uint(s, 8);
        const auto are_ywpr_int8 = is_int(y, 8) && is_int(w, 8) && is_int(p, 8) && is_int(r, 8);
        const auto are_zuqs_int8 = is_int(z, 8) && is_int(u, 8) && is_int(q, 8) && is_int(s, 8);

        const auto can_use_usdot = target.has_feature(Target::ARMv86a) && (are_ywpr_uint8 && are_zuqs_int8);

        // std::cerr << "ArmOptimize Add: " << Expr(op) << "\n";

        if (enable_synthesized_rules() && (

                // TODO: non-accumulating patterns.

                // New UDOT patterns.
                // rewrite(
                //     (widening_add(widening_mul(y, c0), widening_mul(z, c1))
                //      + widening_add(widening_mul(w, c2), widening_mul(u, c3)))
                //       + x,
                //     v_instr(VectorInstruction::dot_product, x, interleave(y, z, w, u), interleave(c0, c1, c2, c3)),
                //     // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                //     (is_uint(x, 32) || is_int(x, 32)) &&
                //     is_uint(y, 8) && is_uint(c0, 8) && is_uint(z, 8) && is_uint(c1, 8) &&
                //     is_uint(w, 8) && is_uint(c2, 8) && is_uint(u, 8) && is_uint(c3, 8)
                // ) ||

                // rewrite(
                //     widening_add(widening_mul(y, c0), widening_mul(z, c1))
                //      + (widening_add(widening_mul(w, c2), widening_mul(u, c3))
                //       + x),
                //     v_instr(VectorInstruction::dot_product, x, interleave(y, z, w, u), interleave(c0, c1, c2, c3)),
                //     // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                //     (is_uint(x, 32) || is_int(x, 32)) &&
                //     is_uint(y, 8) && is_uint(c0, 8) && is_uint(z, 8) && is_uint(c1, 8) &&
                //     is_uint(w, 8) && is_uint(c2, 8) && is_uint(u, 8) && is_uint(c3, 8)
                // ) ||

                // rewrite(
                //     widening_add(widening_mul(y, c0), widening_mul(z, c1))
                //      + (x + widening_add(widening_mul(w, c2), widening_mul(u, c3))),
                //     v_instr(VectorInstruction::dot_product, x, interleave(y, z, w, u), interleave(c0, c1, c2, c3)),
                //     // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                //     (is_uint(x, 32) || is_int(x, 32)) &&
                //     is_uint(y, 8) && is_uint(c0, 8) && is_uint(z, 8) && is_uint(c1, 8) &&
                //     is_uint(w, 8) && is_uint(c2, 8) && is_uint(u, 8) && is_uint(c3, 8)
                // ) ||

                // (a + b) + c
                rewrite(
                    (widening_add(widening_mul(y, z), widening_mul(w, u))
                     + widening_add(widening_mul(p, q), widening_mul(r, s)))
                     + x,
                    v_instr(VectorInstruction::dot_product, x, interleave(y, w, p, r), interleave(z, u, q, s)),
                    // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                    (is_uint(x, 32) || is_int(x, 32)) &&
                    // UDOT
                    (are_ywpr_uint8 && are_zuqs_uint8) ||
                    // SDOT
                    (are_ywpr_int8 && are_zuqs_int8) ||
                    // USDOT
                    can_use_usdot) ||

                // a + (b + c)
                rewrite(
                    widening_add(widening_mul(y, z), widening_mul(w, u))
                     + (widening_add(widening_mul(p, q), widening_mul(r, s))
                     + x),
                    v_instr(VectorInstruction::dot_product, x, interleave(y, w, p, r), interleave(z, u, q, s)),
                    // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                    (is_uint(x, 32) || is_int(x, 32)) &&
                    // UDOT
                    (are_ywpr_uint8 && are_zuqs_uint8) ||
                    // SDOT
                    (are_ywpr_int8 && are_zuqs_int8) ||
                    // USDOT
                    // TODO: need to handle other possibility
                    can_use_usdot) ||

                // a + (c + b)
                rewrite(
                    widening_add(widening_mul(y, z), widening_mul(w, u))
                     + (x + widening_add(widening_mul(p, q), widening_mul(r, s))),
                    v_instr(VectorInstruction::dot_product, x, interleave(y, w, p, r), interleave(z, u, q, s)),
                    // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                    (is_uint(x, 32) || is_int(x, 32)) &&
                    // UDOT
                    (are_ywpr_uint8 && are_zuqs_uint8) ||
                    // SDOT
                    (are_ywpr_int8 && are_zuqs_int8) ||
                    // USDOT
                    // TODO: need to handle other possibility
                    can_use_usdot) ||

                // (a + c) + b
                rewrite(
                    (widening_add(widening_mul(y, z), widening_mul(w, u)) + x)
                     + widening_add(widening_mul(p, q), widening_mul(r, s)),
                    v_instr(VectorInstruction::dot_product, x, interleave(y, w, p, r), interleave(z, u, q, s)),
                    // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                    (is_uint(x, 32) || is_int(x, 32)) &&
                    // UDOT
                    (are_ywpr_uint8 && are_zuqs_uint8) ||
                    // SDOT
                    (are_ywpr_int8 && are_zuqs_int8) ||
                    // USDOT
                    // TODO: need to handle other possibility
                    can_use_usdot) ||

                // (c + a) + b
                rewrite(
                    (x + widening_add(widening_mul(y, z), widening_mul(w, u)))
                     + widening_add(widening_mul(p, q), widening_mul(r, s)),
                    v_instr(VectorInstruction::dot_product, x, interleave(y, w, p, r), interleave(z, u, q, s)),
                    // v_instr(VectorInstruction::dot_product, x, interleave(y, z), interleave(c0, c1)),
                    (is_uint(x, 32) || is_int(x, 32)) &&
                    // UDOT
                    (are_ywpr_uint8 && are_zuqs_uint8) ||
                    // SDOT
                    (are_ywpr_int8 && are_zuqs_int8) ||
                    // USDOT
                    // TODO: need to handle other possibility
                    can_use_usdot) ||
                

                // TODO: do 0-version of dot products.

                // umlal / smlal
                rewrite(x + widening_shift_left(y, c0),
                        x + widening_mul(y, shift_left(narrow_one, c0))) ||
                rewrite(widening_shift_left(y, c0) + x,
                        x + widening_mul(y, shift_left(narrow_one, c0))) ||

                rewrite(cast(op->type, widening_shift_left(y, c0)) + x,
                        x + cast(op->type, typed(flipped_type, widening_mul(y, shift_left(flipped_narrow_one, c0)))),
                        // valid if the cast is not bit-changing (i.e. a reinterpret)
                        is_int(y, bits / 2) || is_uint(y, bits / 2)) ||

                rewrite(reinterpret(op->type, widening_shift_left(y, c0)) + x,
                        x + reinterpret(op->type, typed(flipped_type, widening_mul(y, shift_left(flipped_narrow_one, c0)))),
                        // valid if the cast is not bit-changing (i.e. a reinterpret)
                        is_int(y, bits / 2, lanes) || is_uint(y, bits / 2, lanes)) ||

                // TODO: is this safe? what if we have two widening_muls?
                rewrite(widening_mul(y, z) + x,
                        x + widening_mul(y, z),
                        !is_intrin(x, Call::widening_mul)) ||

        false)) {
            // std::cerr << "matched! " << rewrite.result << "\n";
            return mutate(rewrite.result);
        }

        // This is hard to express as a pattern due to use of lossless_cast.
        if (const VectorReduce *red_a = op->a.as<VectorReduce>()) {
            Expr expr = try_to_use_pwadd_acc(red_a, op->b);
            if (expr.defined()) {
                return mutate(expr);
            }
        }
        if (const VectorReduce *red_b = op->b.as<VectorReduce>()) {
            Expr expr = try_to_use_pwadd_acc(red_b, op->a);
            if (expr.defined()) {
                return mutate(expr);
            }
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const Sub *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        auto rewrite = IRMatcher::rewriter(IRMatcher::sub(op->a, op->b), op->type);

        if (
            rewrite(
                0 - max(x, -127),
                v_instr(VectorInstruction::saturating_negate, x),
                is_int(x, 8)) ||

            rewrite(
                0 - max(x, -32767),
                v_instr(VectorInstruction::saturating_negate, x),
                is_int(x, 16)) ||

            rewrite(
                0 - max(x, -(0x7fffffff)),
                v_instr(VectorInstruction::saturating_negate, x),
                is_int(x, 32)) ||

            false) {
            return mutate(rewrite.result);
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const Cast *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        const int bits = op->type.bits();
        const int lanes = op->type.lanes();

        auto rewrite = IRMatcher::rewriter(IRMatcher::cast(op->type, op->value), op->type);

        // TODO: saturating casts should be intrinsics, and supported in IRMatch.h.

        auto c0_in_shrn_range = (is_uint(c0) || (is_int(c0) && (0 < c0))) && (c0 <= op->type.bits());

        // For shift_right_narrow instructions, aarch64 expectes UInt32 where arm32 expects a signed type.
        const Type shrn_type = target_arm32() ? Int(bits, lanes) : UInt(32);
        const auto shrn_c0 = cast(shrn_type, fold(as_scalar(c0)));

        const Type uint8x_t = UInt(8, lanes);
        const Type uint16x_t = UInt(16, lanes);
        const Type uint32x_t = UInt(32, lanes);
        const Type int8x_t = Int(8, lanes);
        const Type int16x_t = Int(16, lanes);
        const Type int32x_t = Int(32, lanes);

        if (
            // RADDHN - Add and narrow with rounding
            // These must come before other narrowing rounding shift patterns.
            rewrite(
                cast(int8x_t, rounding_shift_right(x + y, 8)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_int(x, 16) && is_int(y, 16)) ||
            rewrite(
                cast(uint8x_t, rounding_shift_right(x + y, 8)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_uint(x, 16) && is_uint(y, 16)) ||
            rewrite(
                cast(int16x_t, rounding_shift_right(x + y, 16)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_int(x, 32) && is_int(y, 32)) ||
            rewrite(
                cast(uint16x_t, rounding_shift_right(x + y, 16)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_uint(x, 32) && is_uint(y, 32)) ||
            rewrite(
                cast(int32x_t, rounding_shift_right(x + y, 32)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_int(x, 64) && is_int(y, 64)) ||
            rewrite(
                cast(uint32x_t, rounding_shift_right(x + y, 32)),
                v_instr(VectorInstruction::rounding_add_narrow, x, y),
                is_uint(x, 64) && is_uint(y, 64)) ||

            // RSUBHN - Add and narrow with rounding
            // These must come before other narrowing rounding shift patterns.
            rewrite(
                cast(int8x_t, rounding_shift_right(x - y, 8)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_int(x, 16) && is_int(y, 16)) ||
            rewrite(
                cast(uint8x_t, rounding_shift_right(x - y, 8)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_uint(x, 16) && is_uint(y, 16)) ||
            rewrite(
                cast(int16x_t, rounding_shift_right(x - y, 16)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_int(x, 32) && is_int(y, 32)) ||
            rewrite(
                cast(uint16x_t, rounding_shift_right(x - y, 16)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_uint(x, 32) && is_uint(y, 32)) ||
            rewrite(
                cast(int32x_t, rounding_shift_right(x - y, 32)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_int(x, 64) && is_int(y, 64)) ||
            rewrite(
                cast(uint32x_t, rounding_shift_right(x - y, 32)),
                v_instr(VectorInstruction::rounding_sub_narrow, x, y),
                is_uint(x, 64) && is_uint(y, 64)) ||

            // RSHRN - Rounding shift right narrow (by immediate in [1, output bits]).
            rewrite(
                cast(int8x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 16) && c0_in_shrn_range) ||
            rewrite(
                cast(uint8x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 16) && c0_in_shrn_range) ||

            // FIXME: CodeGen_ARM.cpp also has: u8(rounding_shift_right(wild_i16x_, wild_u16_)),
            //        and similarly others, but doesn't contain those type signatures in the intrinsics.
            rewrite(
                cast(int16x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 32) && c0_in_shrn_range) ||
            rewrite(
                cast(uint16x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 32) && c0_in_shrn_range) ||
            rewrite(
                cast(int32x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 64) && c0_in_shrn_range) ||
            rewrite(
                cast(uint32x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 64) && c0_in_shrn_range) ||

            // SHRN - Shift right narrow (by immediate in [1, output bits])
            // FIXME: there don't appear to be shift_right_narrow intrinsics in the table.
            //        I also don't see a corresponding LLVM intrinsic for this instruction.

            // ABDL - Widening absolute difference
            // The ARM backend folds both signed and unsigned widening casts of absd to a widening_absd,
            // so we need to handle both signed and unsigned input and return types.
            rewrite(
                cast(UInt(bits, lanes), absd(x, y)),
                v_instr(VectorInstruction::widening_absd, x, y),
                (is_int(x, bits / 2) || is_uint(x, bits / 2)) &&
                    (is_int(y, bits / 2) || is_uint(y, bits / 2)) &&
                    (is_int(x) == is_int(y))) ||

            rewrite(
                cast(Int(bits, lanes), absd(x, y)),
                v_instr(VectorInstruction::widening_absd, x, y),
                (is_int(x, bits / 2) || is_uint(x, bits / 2)) &&
                    (is_int(y, bits / 2) || is_uint(y, bits / 2)) &&
                    (is_int(x) == is_int(y))) ||

            false) {
            return mutate(rewrite.result);
        }
        return InstructionSelector::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        const int lanes = op->type.lanes();
        const int bits = op->type.bits();

        auto rewrite = IRMatcher::rewriter(op, op->type);
        using IRMatcher::typed;

        // Most of the arm intrinsics only have 8, 16, and 32 bit methods.
        auto x_is_small_int = (is_int(x) && !is_int(x, 64));
        auto x_is_small_uint = (is_uint(x) && !is_uint(x, 64));
        auto x_is_small_int_or_uint = x_is_small_int || x_is_small_uint;
        auto y_is_small_int = (is_int(y) && !is_int(y, 64));
        auto y_is_small_uint = (is_uint(y) && !is_uint(y, 64));
        auto y_is_small_int_or_uint = y_is_small_int || y_is_small_uint;

        auto c0_in_shrn_range = (is_uint(c0) || (is_int(c0) && (0 < c0))) && (c0 <= op->type.bits());
        // For shift_right_narrow instructions, aarch64 expectes UInt32 where arm32 expects a signed type.
        const Type shrn_type = target_arm32() ? Int(bits * 2, lanes) : UInt(32);
        const auto shrn_c0 = cast(shrn_type, fold(as_scalar(c0)));

        const Type uint8x_t = UInt(8, lanes);
        const Type uint16x_t = UInt(16, lanes);
        const Type uint32x_t = UInt(32, lanes);
        const Type int8x_t = Int(8, lanes);
        const Type int16x_t = Int(16, lanes);
        const Type int32x_t = Int(32, lanes);

        const Type unsigned_type = op->type.with_code(halide_type_uint);
        // Should only be used if bits > 8.
        const Type unsigned_narrow_type = (bits > 8) ? unsigned_type.narrow() : unsigned_type;

        if (
            rewrite(
                sorted_avg(x, y),
                halving_add(x, y)) ||

            // LLVM wants these as rounding_shift_left with a negative b instead.
            rewrite(
                rounding_shift_right(x, c0),
                rounding_shift_left(x, fold(-c0)),
                is_int(c0)) ||
            // FIXME: we need to simplify the rhs. We currently can't fold casts.
            rewrite(
                rounding_shift_right(x, y),
                rounding_shift_left(x, -cast(Int(bits, lanes), y))) ||

            // We want these as left shifts with a negative b instead.
            rewrite(
                widening_shift_right(x, c0),
                widening_shift_left(x, fold(-c0)),
                is_int(c0)) ||
            // FIXME: we need to simplify the rhs.
            rewrite(
                widening_shift_right(x, y),
                widening_shift_left(x, -y),
                is_int(y)) ||

            // We want these as left shifts with a negative b instead.
            rewrite(
                shift_right(x, c0),
                shift_left(x, fold(-c0)),
                is_int(c0)) ||
            // FIXME: we need to simplify the rhs.
            rewrite(
                shift_right(x, y),
                shift_left(x, -y),
                is_int(y)) ||

            // QDMULH - Saturating doubling multiply keep high half
            rewrite(
                mul_shift_right(x, y, 15),
                v_instr(VectorInstruction::qdmulh, x, y),
                is_int(x, 16) && is_int(y, 16)) ||
            rewrite(
                mul_shift_right(x, y, 31),
                v_instr(VectorInstruction::qdmulh, x, y),
                is_int(x, 32) && is_int(y, 32)) ||

            // QRDMULH - Saturating doubling multiply keep high half with rounding
            rewrite(
                rounding_mul_shift_right(x, y, 15),
                v_instr(VectorInstruction::qrdmulh, x, y),
                is_int(x, 16) && is_int(y, 16)) ||
            rewrite(
                rounding_mul_shift_right(x, y, 31),
                v_instr(VectorInstruction::qrdmulh, x, y),
                is_int(x, 32) && is_int(y, 32)) ||

            rewrite(
                abs(x),
                v_instr(VectorInstruction::abs, x),
                x_is_small_int || is_float(x, 32) || (has_fp16() && is_float(x, 16) && !is_bfloat(x))) ||

            // SABD, UABD - Absolute difference
            rewrite(
                absd(x, y),
                v_instr(VectorInstruction::absd, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SMULL, UMULL - Widening multiply
            rewrite(
                widening_mul(x, y),
                v_instr(VectorInstruction::widening_mul, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint &&
                    // Args must match sign.
                    (is_int(x) == is_int(y))) ||

            // Look for mixed-arg widening_muls with constants.
            (enable_synthesized_rules() &&
             rewrite(
                widening_mul(x, c0),
                reinterpret(op->type,
                 typed(op->type.with_code(halide_type_uint),
                  v_instr(VectorInstruction::widening_mul, x, cast(unsigned_narrow_type, c0)))),
                x_is_small_int_or_uint && (c0 > 0) &&
                    // Args must match sign.
                    (is_uint(x) && is_int(c0)))) ||

            // SQADD, UQADD - Saturating add
            rewrite(
                saturating_add(x, y),
                v_instr(VectorInstruction::saturating_add, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SQSUB, UQSUB - Saturating subtract
            rewrite(
                saturating_sub(x, y),
                v_instr(VectorInstruction::saturating_sub, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SHADD, UHADD - Halving add
            rewrite(
                halving_add(x, y),
                v_instr(VectorInstruction::halving_add, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SHSUB, UHSUB - Halving subtract
            rewrite(
                halving_sub(x, y),
                v_instr(VectorInstruction::halving_sub, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SRHADD, URHADD - Halving add with rounding
            rewrite(
                rounding_halving_add(x, y),
                v_instr(VectorInstruction::rounding_halving_add, x, y),
                x_is_small_int_or_uint && y_is_small_int_or_uint) ||

            // SRSHL, URSHL - Rounding shift left (by signed vector)
            rewrite(
                rounding_shift_left(x, y),
                v_instr(VectorInstruction::rounding_shift_left, x, y),
                is_int(y, bits)) ||
            // TODO: should the rounding_shift_right patterns above be rewritten to instructions?
            // TODO: is there a clean way to rewrite a rounding_shift_left with an unsigned rhs
            //       into this pattern (safely)?

            // SSHL, USHL - Shift left (by signed vector)
            rewrite(
                shift_left(x, y),
                v_instr(VectorInstruction::shift_left, x, y),
                is_int(y, bits)) ||
            // TODO: is there a clean way to rewrite a shift_left with an unsigned rhs
            //       into this pattern (safely)?

            // SQRSHRN, UQRSHRN, SQRSHRUN - Saturating rounding narrowing shift right narrow (by immediate in [1, output bits])
            // SQRSHRN
            rewrite(
                // i8_sat(rounding_shift_right(wild_i16x_, wild_u16_))
                saturating_cast(int8x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // i16_sat(rounding_shift_right(wild_i32x_, wild_u32_))
                saturating_cast(int16x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // i32_sat(rounding_shift_right(wild_i64x_, wild_u64_))
                saturating_cast(int32x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 64) && c0_in_shrn_range) ||
            // UQRSHRN
            rewrite(
                // u8_sat(rounding_shift_right(wild_u16x_, wild_u16_))
                saturating_cast(uint8x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // u16_sat(rounding_shift_right(wild_u32x_, wild_u32_))
                saturating_cast(uint16x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // u32_sat(rounding_shift_right(wild_u64x_, wild_u64_))
                saturating_cast(uint32x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_uint(x, 64) && c0_in_shrn_range) ||
            // SQRSHRUN
            rewrite(
                // u8_sat(rounding_shift_right(wild_i16x_, wild_u16_))
                saturating_cast(uint8x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // u16_sat(rounding_shift_right(wild_i32x_, wild_u32_))
                saturating_cast(uint16x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // u32_sat(rounding_shift_right(wild_i64x_, wild_u64_))
                saturating_cast(uint32x_t, rounding_shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_rounding_shift_right_narrow, x, shrn_c0),
                is_int(x, 64) && c0_in_shrn_range) ||

            // SQSHL, UQSHL, SQSHLU - Saturating shift left by signed register.
            // There is also an immediate version of this - hopefully LLVM does this matching when appropriate.
            // SQSHL
            rewrite(
                // i8_sat(widening_shift_left(wild_i8x_, rhs))
                saturating_cast(int8x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 8)) ||
            rewrite(
                // i16_sat(widening_shift_left(wild_i16x_, rhs))
                saturating_cast(int16x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 16)) ||
            rewrite(
                // i32_sat(widening_shift_left(wild_i23x_, rhs))
                saturating_cast(int32x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 32)) ||
            // UQSHL
            rewrite(
                // u8_sat(widening_shift_left(wild_u8x_, rhs))
                saturating_cast(uint8x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_uint(x, 8)) ||
            rewrite(
                // u16_sat(widening_shift_left(wild_u16x_, rhs))
                saturating_cast(uint16x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_uint(x, 16)) ||
            rewrite(
                // u32_sat(widening_shift_left(wild_u32x_, rhs))
                saturating_cast(uint32x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_uint(x, 32)) ||
            // SQSHLU
            rewrite(
                // u8_sat(widening_shift_left(wild_i8x_, rhs))
                saturating_cast(uint8x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 8)) ||
            rewrite(
                // u16_sat(widening_shift_left(wild_i16x_, rhs))
                saturating_cast(uint16x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 16)) ||
            rewrite(
                // u32_sat(widening_shift_left(wild_i32x_, rhs))
                saturating_cast(uint32x_t, widening_shift_left(x, y)),
                v_instr(VectorInstruction::saturating_shift_left, x, y),
                is_int(x, 32)) ||

            // SQSHRN, UQSHRN, SQSHRUN Saturating narrowing shift right by an (by immediate in [1, output bits])
            // SQSHRN
            rewrite(
                // i8_sat(wild_i16x_ >> wild_u16_)
                saturating_cast(int8x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // i16_sat(wild_i32x_ >> wild_u32_)
                saturating_cast(int16x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // i32_sat(wild_i64x_ >> wild_u64_)
                saturating_cast(int32x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 64) && c0_in_shrn_range) ||
            // UQSHRN
            rewrite(
                // u8_sat(wild_u16x_ >> wild_u16_)
                saturating_cast(uint8x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_uint(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // u16_sat(wild_u32x_ >> wild_u32_)
                saturating_cast(uint16x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_uint(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // u32_sat(wild_u64x_ >> wild_u64_)
                saturating_cast(uint32x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_uint(x, 64) && c0_in_shrn_range) ||
            // SQSHRUN
            rewrite(
                // u8_sat(wild_i16x_ >> wild_u16_)
                saturating_cast(uint8x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 16) && c0_in_shrn_range) ||
            rewrite(
                // u16_sat(wild_i32x_ >> wild_u32_)
                saturating_cast(uint16x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 32) && c0_in_shrn_range) ||
            rewrite(
                // u32_sat(wild_i64x_ >> wild_u64_)
                saturating_cast(uint32x_t, shift_right(x, c0)),
                v_instr(VectorInstruction::saturating_shift_right_narrow, x, shrn_c0),
                is_int(x, 64) && c0_in_shrn_range) ||

            // SQXTN, UQXTN, SQXTUN - Saturating narrow.
            // SQXTN
            rewrite(
                // i8_sat(wild_i16x_)
                saturating_cast(int8x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16)) ||
            rewrite(
                // i16_sat(wild_i32x_)
                saturating_cast(int16x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 32)) ||
            rewrite(
                // i32_sat(wild_i64x_)
                saturating_cast(int32x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 64)) ||
            // UQXTN
            rewrite(
                // u8_sat(wild_u16x_)
                saturating_cast(uint8x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_uint(x, 16)) ||
            rewrite(
                // u16_sat(wild_u32x_)
                saturating_cast(uint16x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_uint(x, 32)) ||
            rewrite(
                // u32_sat(wild_u64x_)
                saturating_cast(uint32x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_uint(x, 64)) ||
            // SQXTUN
            rewrite(
                // u8_sat(wild_i16x_)
                saturating_cast(uint8x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 16)) ||
            rewrite(
                // u16_sat(wild_i32x_)
                saturating_cast(uint16x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 32)) ||
            rewrite(
                // u32_sat(wild_i64x_)
                saturating_cast(uint32x_t, x),
                v_instr(VectorInstruction::saturating_narrow, x),
                is_int(x, 64)) ||

            // If we didn't find a pattern, try rewriting the cast.
            // Double or triple narrowing saturating casts are better expressed as
            // regular narrowing casts.
            rewrite(
                // u8_sat(wild_u32x_) -> u8_sat(u16_sat(wild_u32x_))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(uint16x_t, x)),
                is_uint(x, 32)) ||
            rewrite(
                // u8_sat(wild_i32x_) -> u8_sat(i16_sat(wild_i32x_))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(int16x_t, x)),
                is_int(x, 32)) ||
            rewrite(
                // u8_sat(wild_f32x_) -> u8_sat(i16_sat(wild_f32x_))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(int16x_t, x)),
                is_float(x, 32)) ||
            rewrite(
                // i8_sat(wild_u32x_) -> i8_sat(u16_sat(wild_u32x_))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(uint16x_t, x)),
                is_uint(x, 32)) ||
            rewrite(
                // i8_sat(wild_i32x_) -> i8_sat(i16_sat(wild_i32x_))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(int16x_t, x)),
                is_int(x, 32)) ||
            rewrite(
                // i8_sat(wild_f32x_) -> i8_sat(i16_sat(wild_f32x_))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(int16x_t, x)),
                is_float(x, 32)) ||
            rewrite(
                // u16_sat(wild_u64x_) -> u16_sat(u32_sat(wild_u64x_))
                saturating_cast(uint16x_t, x),
                saturating_cast(uint16x_t, saturating_cast(uint32x_t, x)),
                is_uint(x, 64)) ||
            rewrite(
                // u16_sat(wild_i64x_) -> u16_sat(i32_sat(wild_i64x_))
                saturating_cast(uint16x_t, x),
                saturating_cast(uint16x_t, saturating_cast(int32x_t, x)),
                is_int(x, 64)) ||
            rewrite(
                // u16_sat(wild_f64x_) -> u16_sat(i32_sat(wild_f64x_))
                saturating_cast(uint16x_t, x),
                saturating_cast(uint16x_t, saturating_cast(int32x_t, x)),
                is_float(x, 64)) ||
            rewrite(
                // i16_sat(wild_u64x_) -> i16_sat(u32_sat(wild_u64x_))
                saturating_cast(int16x_t, x),
                saturating_cast(int16x_t, saturating_cast(uint32x_t, x)),
                is_uint(x, 64)) ||
            rewrite(
                // i16_sat(wild_i64x_) -> i16_sat(i32_sat(wild_i64x_))
                saturating_cast(int16x_t, x),
                saturating_cast(int16x_t, saturating_cast(int32x_t, x)),
                is_int(x, 64)) ||
            rewrite(
                // i16_sat(wild_f64x_) -> i16_sat(i32_sat(wild_f64x_))
                saturating_cast(int16x_t, x),
                saturating_cast(int16x_t, saturating_cast(int32x_t, x)),
                is_float(x, 64)) ||
            rewrite(
                // u8_sat(wild_u64x_) -> u8_sat(u16_sat(u32_sat(wild_u64x_)))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(uint16x_t, saturating_cast(uint32x_t, x))),
                is_uint(x, 64)) ||
            rewrite(
                // u8_sat(wild_i64x_) -> u8_sat(i16_sat(i32_sat(wild_i64x_)))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(int16x_t, saturating_cast(int32x_t, x))),
                is_int(x, 64)) ||
            rewrite(
                // u8_sat(wild_f64x_), u8_sat(i16_sat(i32_sat(wild_f64x_)))
                saturating_cast(uint8x_t, x),
                saturating_cast(uint8x_t, saturating_cast(int16x_t, saturating_cast(int32x_t, x))),
                is_float(x, 64)) ||
            rewrite(
                // i8_sat(wild_u64x_) -> i8_sat(u16_sat(u32_sat(wild_u64x_)))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(uint16x_t, saturating_cast(uint32x_t, x))),
                is_uint(x, 64)) ||
            rewrite(
                // i8_sat(wild_i64x_) -> i8_sat(i16_sat(i32_sat(wild_i64x_)))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(int16x_t, saturating_cast(int32x_t, x))),
                is_int(x, 64)) ||
            rewrite(
                // i8_sat(wild_f64x_) -> i8_sat(i16_sat(i32_sat(wild_f64x_)))
                saturating_cast(int8x_t, x),
                saturating_cast(int8x_t, saturating_cast(int16x_t, saturating_cast(int32x_t, x))),
                is_float(x, 64)) ||

            false) {
            return mutate(rewrite.result);
        }

        // FIXME: this only works if we are top-down.
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
            // TODO: Should the base InstructionSelector class perform this lowering?
            return mutate(lower_intrinsic(op));
        }

        return InstructionSelector::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        if ((op->op != VectorReduce::Add &&
             op->op != VectorReduce::Min &&
             op->op != VectorReduce::Max) ||
            !should_peephole_optimize(op->type)) {
            return InstructionSelector::visit(op);
        }

        const int lanes = op->type.lanes();
        const int value_lanes = op->value.type().lanes();
        const int factor = value_lanes / lanes;
        Expr value = op->value;

        const Expr i8_1 = make_const(Int(8, lanes * 4), 1);
        const Expr u8_1 = make_const(UInt(8, lanes * 4), 1);

        switch (op->op) {
        case VectorReduce::Add: {
            auto rewrite = IRMatcher::rewriter(IRMatcher::h_add(value, lanes), op->type);
            const Expr zero = make_zero(op->type);

            if (target.has_feature(Target::ARMDotProd) &&
                (
                    // SDOT
                    rewrite(
                        h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, y),
                        is_int(x, 8, lanes * 4) && is_int(y, 8, lanes * 4)) ||
                    // UDOT
                    rewrite(
                        h_add(cast(Int(32, lanes * 4), widening_mul(x, y)), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, y),
                        is_uint(x, 8, lanes * 4) && is_uint(y, 8, lanes * 4)) ||
                    rewrite(
                        h_add(cast(UInt(32, lanes * 4), widening_mul(x, y)), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, y),
                        is_uint(x, 8, lanes * 4) && is_uint(y, 8, lanes * 4)) ||

                    // TODO: ARM also has sudot: https://developer.arm.com/documentation/ddi0602/2022-03/SIMD-FP-Instructions/SUDOT--by-element---Dot-product-with-signed-and-unsigned-integers--vector--by-element--
                    //       Unfortunately, I only see LLVM intrinsics for the SVE version.

                    // A sum is the same as a dot product with a vector of ones, and this appears to
                    // be a bit faster.
                    // SDOT
                    rewrite(
                        h_add(cast(Int(32, lanes * 4), x), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, i8_1),
                        is_int(x, 8, lanes * 4)) ||
                    // UDOT
                    rewrite(
                        h_add(cast(Int(32, lanes * 4), x), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, u8_1),
                        is_uint(x, 8, lanes * 4)) ||
                    rewrite(
                        h_add(cast(UInt(32, lanes * 4), x), lanes),
                        v_instr(VectorInstruction::dot_product, zero, x, u8_1),
                        is_uint(x, 8, lanes * 4)) ||

                    false)) {
                return mutate(rewrite.result);
            }

            // CodeGen_ARM had custom logic for splitting up VectorReduces, we need
            // to emulate that logic here as well.
            const int dp_factor = 4;  // All ARM dot_product instructions have factor=4.
            if (target.has_feature(Target::ARMDotProd) && (factor % 4 == 0)) {
                // Check for any of the above matching patterns.

                // TODO: is this the right logic? It follows CodeGen_ARM but it seems
                //       sub-optimal.
                const int reduce_factor = op->value.type().lanes() / dp_factor;

                // TODO: why do we need this in order to use h_add in the rhs?
                using IRMatcher::h_add;

                if (
                    // dot_products
                    rewrite(
                        h_add(cast(Int(32, value_lanes), widening_mul(x, y)), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_int(x, 8, value_lanes) && is_int(y, 8, value_lanes)) ||
                    rewrite(
                        h_add(cast(Int(32, value_lanes), widening_mul(x, y)), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_uint(x, 8, value_lanes) && is_uint(y, 8, value_lanes)) ||
                    rewrite(
                        h_add(cast(UInt(32, value_lanes), widening_mul(x, y)), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_uint(x, 8, value_lanes) && is_uint(y, 8, value_lanes)) ||

                    // TODO: SUDOT pattern (?)

                    // sums
                    rewrite(
                        h_add(cast(Int(32, value_lanes), x), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_int(x, 8, value_lanes)) ||
                    rewrite(
                        h_add(cast(Int(32, value_lanes), x), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_uint(x, 8, value_lanes)) ||
                    rewrite(
                        h_add(cast(UInt(32, value_lanes), x), lanes),
                        h_add(h_add(op->value, reduce_factor), lanes),
                        is_uint(x, 8, value_lanes)) ||

                    false) {
                    return mutate(rewrite.result);
                }
            }

            // TODO: This is hard to write as patterns, solely due to lossless_cast. We really need a
            // can_losslessly_cast predicate, but it is very expensive.
            if (factor == 2) {
                Type narrow_type = op->type.narrow().with_lanes(op->value.type().lanes());
                Expr narrow = lossless_cast(narrow_type, op->value);
                if (!narrow.defined() && op->type.is_int()) {
                    // We can also safely accumulate from a uint into a
                    // wider int, because the addition uses at most one
                    // extra bit.
                    narrow = lossless_cast(narrow_type.with_code(Type::UInt), op->value);
                }
                const bool use_float64_pwadd = !target_arm32() && op->type.element_of() == Float(64);
                const bool use_float32_pwadd = op->type.element_of() == Float(32);
                const bool use_float16_pwadd = has_fp16() && op->type.element_of() == Float(16);
                const bool can_pairwise_add_float = use_float16_pwadd || use_float32_pwadd || use_float64_pwadd;
                const bool can_pairwise_add_int = (op->type.is_int_or_uint() && (op->type.bits() <= 32));
                if (narrow.defined() && op->type.is_int_or_uint()) {
                    return mutate(VectorInstruction::make(op->type, VectorInstruction::pairwise_widening_add, {narrow}));
                } else if (can_pairwise_add_int || can_pairwise_add_float) {
                    return mutate(VectorInstruction::make(op->type, VectorInstruction::pairwise_add, {op->value}));
                }
            }

            break;
        }
        case VectorReduce::Max: {
            // This really doesn't need to be a rewrite, but for completeness...
            auto rewrite = IRMatcher::rewriter(IRMatcher::h_max(value, lanes), op->type);
            // Supported for (u)int(8 | 16 | 32) and float(16 | 32)
            auto x_is_small_int_x2 = (is_int(x, 0, lanes * 2) && !is_int(x, 64));
            auto x_is_small_uint_x2 = (is_uint(x, 0, lanes * 2) && !is_uint(x, 64));
            auto x_is_small_float_x2 = (has_fp16() && is_float(x, 16, lanes * 2) && !is_bfloat(x)) || is_float(x, 32, lanes * 2);

            if (
                // SMAXP, UMAXP, FMAXP - Pairwise max.
                rewrite(
                    h_max(x, lanes),
                    v_instr(VectorInstruction::pairwise_max, x),
                    x_is_small_int_x2 || x_is_small_uint_x2 || x_is_small_float_x2) ||

                false) {
                return mutate(rewrite.result);
            }
            break;
        }
        case VectorReduce::Min: {
            // This really doesn't need to be a rewrite, but for completeness...
            auto rewrite = IRMatcher::rewriter(IRMatcher::h_min(value, lanes), op->type);
            auto x_is_small_int_x2 = (is_int(x, 0, lanes * 2) && !is_int(x, 64));
            auto x_is_small_uint_x2 = (is_uint(x, 0, lanes * 2) && !is_uint(x, 64));
            auto x_is_small_float_x2 = (has_fp16() && is_float(x, 16, lanes * 2) && !is_bfloat(x)) || is_float(x, 32, lanes * 2);

            if (
                // SMINP, UMINP, FMINP - Pairwise min.
                rewrite(
                    h_min(x, lanes),
                    v_instr(VectorInstruction::pairwise_min, x),
                    x_is_small_int_x2 || x_is_small_uint_x2 || x_is_small_float_x2) ||

                false) {
                return mutate(rewrite.result);
            }
            break;
        }
        default: {
            internal_error << "unreachable";
        }
        }

        return InstructionSelector::visit(op);
    }

private:
    IRMatcher::Wild<0> x;
    IRMatcher::Wild<1> y;
    IRMatcher::Wild<2> z;
    IRMatcher::Wild<3> w;
    IRMatcher::Wild<4> u;
    IRMatcher::Wild<5> p;
    IRMatcher::Wild<6> q;
    IRMatcher::Wild<7> r;
    IRMatcher::Wild<8> s;
    // IRMatcher::Wild<9> t;
    IRMatcher::WildConst<0> c0;
    // IRMatcher::WildConst<1> c1;
    // IRMatcher::WildConst<2> c2;
    // IRMatcher::WildConst<3> c3;
};

}  // namespace

Stmt optimize_arm_instructions(const Stmt &s, const Target &target, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb) {
    if (get_env_variable("HL_DISABLE_ARM_LOWERING") == "1") {
        return s;
    }

    Stmt stmt = Optimize_ARM(target, codegen, fvb).mutate(s);

    if (!stmt.same_as(s)) {
        return stmt;
    } else {
        return s;
    }
}

#else  // WITH_ARM

Stmt optimize_arm_instructions(const Stmt &s, const Target &t, const CodeGen_LLVM *codegen, const FuncValueBounds &fvb) {
    user_error << "ARM not enabled for this build of Halide.\n";
    return Stmt();
}

#endif  // WITH_ARM

}  // namespace Internal
}  // namespace Halide

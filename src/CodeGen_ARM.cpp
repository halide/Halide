#include <iostream>
#include <sstream>

#include "CodeGen_ARM.h"
#include "ConciseCasts.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;
using std::pair;

using namespace Halide::ConciseCasts;
using namespace llvm;

CodeGen_ARM::CodeGen_ARM(Target target) : CodeGen_Posix(target) {
    if (target.bits == 32) {
        #if !(WITH_ARM)
        user_error << "arm not enabled for this build of Halide.";
        #endif
        user_assert(llvm_ARM_enabled) << "llvm build not configured with ARM target enabled\n.";
    } else {
        #if !(WITH_AARCH64)
        user_error << "aarch64 not enabled for this build of Halide.";
        #endif
        user_assert(llvm_AArch64_enabled) << "llvm build not configured with AArch64 target enabled.\n";
    }

    // Generate the cast patterns that can take vector types.  We need
    // to iterate over all 64 and 128 bit integer types relevant for
    // neon.
    Type types[] = {Int(8, 8), Int(8, 16), UInt(8, 8), UInt(8, 16),
        Int(16, 4), Int(16, 8), UInt(16, 4), UInt(16, 8),
        Int(32, 2), Int(32, 4), UInt(32, 2), UInt(32, 4)};
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        Type t = types[i];

        int intrin_lanes = t.lanes();
        std::ostringstream oss;
        oss << ".v" << intrin_lanes << "i" << t.bits();
        string t_str = oss.str();

        // For the 128-bit versions, we want to match any vector width.
        if (t.bits() * t.lanes() == 128) {
            t = t.with_lanes(0);
        }

        // Wider versions of the type
        Type w = t.with_bits(t.bits() * 2);
        Type ws = Int(t.bits()*2, t.lanes());

        // Vector wildcard for this type
        Expr vector = Variable::make(t, "*");
        Expr w_vector = Variable::make(w, "*");
        Expr ws_vector = Variable::make(ws, "*");

        // Bounds of the type stored in the wider vector type
        Expr tmin = simplify(cast(w, t.min()));
        Expr tmax = simplify(cast(w, t.max()));
        Expr tsmin = simplify(cast(ws, t.min()));
        Expr tsmax = simplify(cast(ws, t.max()));

        Pattern p("", "", intrin_lanes, Expr(), Pattern::NarrowArgs);

        // Rounding-up averaging
        if (t.is_int()) {
            p.intrin32 = "llvm.arm.neon.vrhadds" + t_str;
            p.intrin64 = "llvm.aarch64.neon.srhadd" + t_str;
        } else {
            p.intrin32 = "llvm.arm.neon.vrhaddu" + t_str;
            p.intrin64 = "llvm.aarch64.neon.urhadd" + t_str;
        }

        p.pattern = cast(t, (w_vector + w_vector + 1)/2);
        casts.push_back(p);
        p.pattern = cast(t, (w_vector + (w_vector + 1))/2);
        casts.push_back(p);
        p.pattern = cast(t, ((w_vector + 1) + w_vector)/2);
        casts.push_back(p);

        // Rounding down averaging
        if (t.is_int()) {
            p.intrin32 = "llvm.arm.neon.vhadds" + t_str;
            p.intrin64 = "llvm.aarch64.neon.shadd" + t_str;
        } else {
            p.intrin32 = "llvm.arm.neon.vhaddu" + t_str;
            p.intrin64 = "llvm.aarch64.neon.uhadd" + t_str;
        }
        p.pattern = cast(t, (w_vector + w_vector)/2);
        casts.push_back(p);

        // Halving subtract
        if (t.is_int()) {
            p.intrin32 = "llvm.arm.neon.vhsubs" + t_str;
            p.intrin64 = "llvm.aarch64.neon.shsub" + t_str;
        } else {
            p.intrin32 = "llvm.arm.neon.vhsubu" + t_str;
            p.intrin64 = "llvm.aarch64.neon.uhsub" + t_str;
        }
        p.pattern = cast(t, (w_vector - w_vector)/2);
        casts.push_back(p);

        // Saturating add
        if (t.is_int()) {
            p.intrin32 = "llvm.arm.neon.vqadds" + t_str;
            p.intrin64 = "llvm.aarch64.neon.sqadd" + t_str;
        } else {
            p.intrin32 = "llvm.arm.neon.vqaddu" + t_str;
            p.intrin64 = "llvm.aarch64.neon.uqadd" + t_str;
        }
        p.pattern = cast(t, clamp(w_vector + w_vector, tmin, tmax));
        casts.push_back(p);

        // In the unsigned case, the saturation below is unnecessary
        if (t.is_uint()) {
            p.pattern = cast(t, min(w_vector + w_vector, tmax));
            casts.push_back(p);
        }

        // Saturating subtract
        // N.B. Saturating subtracts always widen to a signed type
        if (t.is_int()) {
            p.intrin32 = "llvm.arm.neon.vqsubs" + t_str;
            p.intrin64 = "llvm.aarch64.neon.sqsub" + t_str;
        } else {
            p.intrin32 = "llvm.arm.neon.vqsubu" + t_str;
            p.intrin64 = "llvm.aarch64.neon.uqsub" + t_str;
        }
        p.pattern = cast(t, clamp(ws_vector - ws_vector, tsmin, tsmax));
        casts.push_back(p);

        // In the unsigned case, we may detect that the top of the clamp is unnecessary
        if (t.is_uint()) {
            p.pattern = cast(t, max(ws_vector - ws_vector, 0));
            casts.push_back(p);
        }
    }

    casts.push_back(Pattern("vqrdmulh.v4i16", "sqrdmulh.v4i16", 4,
                            i16_sat((wild_i32x4 * wild_i32x4 + (1<<14)) / (1 << 15)),
                            Pattern::NarrowArgs));
    casts.push_back(Pattern("vqrdmulh.v8i16", "sqrdmulh.v8i16", 8,
                            i16_sat((wild_i32x_ * wild_i32x_ + (1<<14)) / (1 << 15)),
                            Pattern::NarrowArgs));
    casts.push_back(Pattern("vqrdmulh.v2i32", "sqrdmulh.v2i32", 2,
                            i32_sat((wild_i64x2 * wild_i64x2 + (1<<30)) / Expr(int64_t(1) << 31)),
                            Pattern::NarrowArgs));
    casts.push_back(Pattern("vqrdmulh.v4i32", "sqrdmulh.v4i32", 4,
                            i32_sat((wild_i64x_ * wild_i64x_ + (1<<30)) / Expr(int64_t(1) << 31)),
                            Pattern::NarrowArgs));

    casts.push_back(Pattern("vqshiftns.v8i8",  "sqshrn.v8i8",  8, i8_sat(wild_i16x_/wild_i16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftns.v4i16", "sqshrn.v4i16", 4, i16_sat(wild_i32x_/wild_i32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftns.v2i32", "sqshrn.v2i32", 2, i32_sat(wild_i64x_/wild_i64x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v8i8",  "uqshrn.v8i8",  8, u8_sat(wild_u16x_/wild_u16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v4i16", "uqshrn.v4i16", 4, u16_sat(wild_u32x_/wild_u32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v2i32", "uqshrn.v2i32", 2, u32_sat(wild_u64x_/wild_u64x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v8i8",  "sqshrun.v8i8",  8, u8_sat(wild_i16x_/wild_i16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v4i16", "sqshrun.v4i16", 4, u16_sat(wild_i32x_/wild_i32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v2i32", "sqshrun.v2i32", 2, u32_sat(wild_i64x_/wild_i64x_), Pattern::RightShift));

    // Where a 64-bit and 128-bit version exist, we use the 64-bit
    // version only when the args are 64-bits wide.
    casts.push_back(Pattern("vqshifts.v8i8",  "sqshl.v8i8",  8, i8_sat(i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v4i16", "sqshl.v4i16", 4, i16_sat(i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v2i32", "sqshl.v2i32", 2, i32_sat(i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v8i8",  "uqshl.v8i8",  8, u8_sat(u16(wild_u8x8)*wild_u16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v4i16", "uqshl.v4i16", 4, u16_sat(u32(wild_u16x4)*wild_u32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v2i32", "uqshl.v2i32", 2, u32_sat(u64(wild_u32x2)*wild_u64x2), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v8i8",  "sqshlu.v8i8",  8, u8_sat(i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v4i16", "sqshlu.v4i16", 4, u16_sat(i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v2i32", "sqshlu.v2i32", 2, u32_sat(i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));

    // We use the 128-bit version for all other vector widths.
    casts.push_back(Pattern("vqshifts.v16i8", "sqshl.v16i8", 16, i8_sat(i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v8i16", "sqshl.v8i16",  8, i16_sat(i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v4i32", "sqshl.v4i32",  4, i32_sat(i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v16i8", "uqshl.v16i8",  16, u8_sat(u16(wild_u8x_)*wild_u16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v8i16", "uqshl.v8i16",  8, u16_sat(u32(wild_u16x_)*wild_u32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v4i32", "uqshl.v4i32",  4, u32_sat(u64(wild_u32x_)*wild_u64x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v16i8", "sqshlu.v16i8", 16, u8_sat(i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v8i16", "sqshlu.v8i16", 8, u16_sat(i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v4i32", "sqshlu.v4i32", 4, u32_sat(i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));

    casts.push_back(Pattern("vqmovns.v8i8",  "sqxtn.v8i8",    8,   i8_sat(wild_i16x_)));
    casts.push_back(Pattern("vqmovns.v4i16", "sqxtn.v4i16",   4,  i16_sat(wild_i32x_)));
    casts.push_back(Pattern("vqmovns.v2i32", "sqxtn.v2i32",   2,  i32_sat(wild_i64x_)));
    casts.push_back(Pattern("vqmovnu.v8i8",  "uqxtn.v8i8",    8,   u8_sat(wild_u16x_)));
    casts.push_back(Pattern("vqmovnu.v4i16", "uqxtn.v4i16",   4,  u16_sat(wild_u32x_)));
    casts.push_back(Pattern("vqmovnu.v2i32", "uqxtn.v2i32",   2,  u32_sat(wild_u64x_)));
    casts.push_back(Pattern("vqmovnsu.v8i8",  "sqxtun.v8i8",  8,   u8_sat(wild_i16x_)));
    casts.push_back(Pattern("vqmovnsu.v4i16", "sqxtun.v4i16", 4,  u16_sat(wild_i32x_)));
    casts.push_back(Pattern("vqmovnsu.v2i32", "sqxtun.v2i32", 2,  u32_sat(wild_i64x_)));

    // Overflow for int32 is not defined by Halide, so for those we can take
    // advantage of special add-and-halve instructions.
    //
    // 64-bit averaging round-down
    averagings.push_back(Pattern("vhadds.v2i32", "shadd.v2i32", 2, (wild_i32x2 + wild_i32x2)));

    // 128-bit
    averagings.push_back(Pattern("vhadds.v4i32", "shadd.v4i32", 4, (wild_i32x_ + wild_i32x_)));

    // 64-bit halving subtract
    averagings.push_back(Pattern("vhsubs.v2i32", "shsub.v2i32", 2, (wild_i32x2 - wild_i32x2)));

    // 128-bit
    averagings.push_back(Pattern("vhsubs.v4i32", "shsub.v4i32", 4, (wild_i32x_ - wild_i32x_)));

    // 64-bit saturating negation
    negations.push_back(Pattern("vqneg.v8i8",  "sqneg.v8i8", 8, -max(wild_i8x8, -127)));
    negations.push_back(Pattern("vqneg.v4i16", "sqneg.v4i16", 4, -max(wild_i16x4, -32767)));
    negations.push_back(Pattern("vqneg.v2i32", "sqneg.v2i32", 2, -max(wild_i32x2, -(0x7fffffff))));

    // 128-bit
    negations.push_back(Pattern("vqneg.v16i8", "sqneg.v16i8", 16, -max(wild_i8x_, -127)));
    negations.push_back(Pattern("vqneg.v8i16", "sqneg.v8i16", 8,  -max(wild_i16x_, -32767)));
    negations.push_back(Pattern("vqneg.v4i32", "sqneg.v4i32", 4,  -max(wild_i32x_, -(0x7fffffff))));
}

Value *CodeGen_ARM::call_pattern(const Pattern &p, Type t, const vector<Expr> &args) {
    if (target.bits == 32) {
        return call_intrin(t, p.intrin_lanes, p.intrin32, args);
    } else {
        return call_intrin(t, p.intrin_lanes, p.intrin64, args);
    }
}

Value *CodeGen_ARM::call_pattern(const Pattern &p, llvm::Type *t, const vector<llvm::Value *> &args) {
    if (target.bits == 32) {
        return call_intrin(t, p.intrin_lanes, p.intrin32, args);
    } else {
        return call_intrin(t, p.intrin_lanes, p.intrin64, args);
    }
}

void CodeGen_ARM::visit(const Cast *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    Type t = op->type;

    vector<Expr> matches;

    for (size_t i = 0; i < casts.size() ; i++) {
        const Pattern &pattern = casts[i];
        //debug(4) << "Trying pattern: " << patterns[i].intrin << " " << patterns[i].pattern << "\n";
        if (expr_match(pattern.pattern, op, matches)) {

            //debug(4) << "Match!\n";
            if (pattern.type == Pattern::Simple) {
                value = call_pattern(pattern, t, matches);
                return;
            } else if (pattern.type == Pattern::NarrowArgs) {
                // Try to narrow all of the args.
                bool all_narrow = true;
                for (size_t i = 0; i < matches.size(); i++) {
                    internal_assert(matches[i].type().bits() == t.bits() * 2);
                    internal_assert(matches[i].type().lanes() == t.lanes());
                    // debug(4) << "Attemping to narrow " << matches[i] << " to " << t << "\n";
                    matches[i] = lossless_cast(t, matches[i]);
                    if (!matches[i].defined()) {
                        // debug(4) << "failed\n";
                        all_narrow = false;
                    } else {
                        // debug(4) << "success: " << matches[i] << "\n";
                        internal_assert(matches[i].type() == t);
                    }
                }

                if (all_narrow) {
                    value = call_pattern(pattern, t, matches);
                    return;
                }
            } else { // must be a shift
                Expr constant = matches[1];
                int shift_amount;
                bool power_of_two = is_const_power_of_two_integer(constant, &shift_amount);
                if (power_of_two && shift_amount < matches[0].type().bits()) {
                    if (target.bits == 32 && pattern.type == Pattern::RightShift) {
                        // The arm32 llvm backend wants right shifts to come in as negative values.
                        shift_amount = -shift_amount;
                    }
                    Value *shift = nullptr;
                    // The arm64 llvm backend wants i32 constants for right shifts.
                    if (target.bits == 64 && pattern.type == Pattern::RightShift) {
                        shift = ConstantInt::get(i32_t, shift_amount);
                    } else {
                        shift = ConstantInt::get(llvm_type_of(matches[0].type()), shift_amount);
                    }
                    value = call_pattern(pattern, llvm_type_of(t),
                                         {codegen(matches[0]), shift});
                    return;
                }
            }
        }
    }


    // Catch extract-high-half-of-signed integer pattern and convert
    // it to extract-high-half-of-unsigned-integer. llvm peephole
    // optimization recognizes logical shift right but not arithemtic
    // shift right for this pattern. This matters for vaddhn of signed
    // integers.
    if (t.is_vector() &&
        (t.is_int() || t.is_uint()) &&
        op->value.type().is_int() &&
        t.bits() == op->value.type().bits() / 2) {
        const Div *d = op->value.as<Div>();
        if (d && is_const(d->b, int64_t(1) << t.bits())) {
            Type unsigned_type = UInt(t.bits() * 2, t.lanes());
            Expr replacement = cast(t,
                                    cast(unsigned_type, d->a) /
                                    cast(unsigned_type, d->b));
            replacement.accept(this);
            return;
        }
    }

    // Catch widening of absolute difference
    if (t.is_vector() &&
        (t.is_int() || t.is_uint()) &&
        (op->value.type().is_int() || op->value.type().is_uint()) &&
        t.bits() == op->value.type().bits() * 2) {
        Expr a, b;
        const Call *c = op->value.as<Call>();
        if (c && c->is_intrinsic(Call::absd)) {
            ostringstream ss;
            int intrin_lanes = 128 / t.bits();
            ss << "vabdl_" << (c->args[0].type().is_int() ? 'i' : 'u') << t.bits() / 2 << 'x' << intrin_lanes;
            value = call_intrin(t, intrin_lanes, ss.str(), c->args);
            return;
        }
    }

    CodeGen_Posix::visit(op);

}

void CodeGen_ARM::visit(const Mul *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    // We only have peephole optimizations for int vectors for now
    if (op->type.is_scalar() || op->type.is_float()) {
        CodeGen_Posix::visit(op);
        return;
    }

    // Vector multiplies by 3, 5, 7, 9 should do shift-and-add or
    // shift-and-sub instead to reduce register pressure (the
    // shift is an immediate)
    if (is_const(op->b, 3)) {
        value = codegen(op->a*2 + op->a);
        return;
    } else if (is_const(op->b, 5)) {
        value = codegen(op->a*4 + op->a);
        return;
    } else if (is_const(op->b, 7)) {
        value = codegen(op->a*8 - op->a);
        return;
    } else if (is_const(op->b, 9)) {
        value = codegen(op->a*8 + op->a);
        return;
    }

    vector<Expr> matches;

    int shift_amount = 0;
    bool power_of_two = is_const_power_of_two_integer(op->b, &shift_amount);
    if (power_of_two) {
        for (size_t i = 0; i < left_shifts.size(); i++) {
            const Pattern &pattern = left_shifts[i];
            internal_assert(pattern.type == Pattern::LeftShift);
            if (expr_match(pattern.pattern, op, matches)) {
                llvm::Type *t_arg = llvm_type_of(matches[0].type());
                llvm::Type *t_result = llvm_type_of(op->type);
                Value *shift = nullptr;
                if (target.bits == 32) {
                    shift = ConstantInt::get(t_arg, shift_amount);
                } else {
                    shift = ConstantInt::get(i32_t, shift_amount);
                }
                value = call_pattern(pattern, t_result,
                                     {codegen(matches[0]), shift});
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

Expr CodeGen_ARM::sorted_avg(Expr a, Expr b) {
    Type ty = a.type();
    Type wide_ty = ty.with_bits(ty.bits() * 2);
    // This will codegen to vhaddu (arm32) or uhadd (arm64).
    return cast(ty, (cast(wide_ty, a) + cast(wide_ty, b))/2);
}

void CodeGen_ARM::visit(const Div *op) {
    if (!neon_intrinsics_disabled() &&
        op->type.is_vector() && is_two(op->b) &&
        (op->a.as<Add>() || op->a.as<Sub>())) {
        vector<Expr> matches;
        for (size_t i = 0; i < averagings.size(); i++) {
            if (expr_match(averagings[i].pattern, op->a, matches)) {
                value = call_pattern(averagings[i], op->type, matches);
                return;
            }
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Add *op) {
    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Sub *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    vector<Expr> matches;
    for (size_t i = 0; i < negations.size(); i++) {
        if (op->type.is_vector() &&
            expr_match(negations[i].pattern, op, matches)) {
            value = call_pattern(negations[i], op->type, matches);
            return;
        }
    }

    // llvm will generate floating point negate instructions if we ask for (-0.0f)-x
    if (op->type.is_float() && is_zero(op->a)) {
        Constant *a;
        if (op->type.bits() == 32) {
            a = ConstantFP::getNegativeZero(f32_t);
        } else if (op->type.bits() == 64) {
            a = ConstantFP::getNegativeZero(f64_t);
        } else {
            a = nullptr;
            internal_error << "Unknown bit width for floating point type: " << op->type << "\n";
        }

        Value *b = codegen(op->b);

        if (op->type.lanes() > 1) {
            a = ConstantVector::getSplat(op->type.lanes(), a);
        }
        value = builder->CreateFSub(a, b);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Mod *op) {
     int bits;
     if (op->type.is_int() &&
         op->type.is_vector() &&
         target.bits == 32 &&
         !is_const_power_of_two_integer(op->b, &bits)) {
         // 32-bit arm has no vectorized integer modulo, and attempting
         // to codegen one seems to tickle an llvm bug in some cases.
         scalarize(op);
     } else {
         CodeGen_Posix::visit(op);
     }
}

void CodeGen_ARM::visit(const Min *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->type == Float(32)) {
        // Use a 2-wide vector instead
        Value *undef = UndefValue::get(f32x2);
        Constant *zero = ConstantInt::get(i32_t, 0);
        Value *a_wide = builder->CreateInsertElement(undef, codegen(op->a), zero);
        Value *b_wide = builder->CreateInsertElement(undef, codegen(op->b), zero);
        Value *wide_result;
        if (target.bits == 32) {
            wide_result = call_intrin(f32x2, 2, "llvm.arm.neon.vmins.v2f32", {a_wide, b_wide});
        } else {
            wide_result = call_intrin(f32x2, 2, "llvm.aarch64.neon.fmin.v2f32", {a_wide, b_wide});
        }
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "v8i8"},
        {UInt(16, 4), "v4i16"},
        {UInt(32, 2), "v2i32"},
        {Int(8, 8), "v8i8"},
        {Int(16, 4), "v4i16"},
        {Int(32, 2), "v2i32"},
        {Float(32, 2), "v2f32"},
        {UInt(8, 16), "v16i8"},
        {UInt(16, 8), "v8i16"},
        {UInt(32, 4), "v4i32"},
        {Int(8, 16), "v16i8"},
        {Int(16, 8), "v8i16"},
        {Int(32, 4), "v4i32"},
        {Float(32, 4), "v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        bool match = op->type == patterns[i].t;

        // The 128-bit versions are also used for other vector widths.
        if (op->type.is_vector() && patterns[i].t.lanes() * patterns[i].t.bits() == 128) {
            match = match || (op->type.element_of() == patterns[i].t.element_of());
        }

        if (match) {
            string intrin;
            if (target.bits == 32) {
                intrin = (string("llvm.arm.neon.") + (op->type.is_uint() ? "vminu." : "vmins.")) + patterns[i].op;
            } else {
                intrin = "llvm.aarch64.neon.";
                if (op->type.is_int()) {
                    intrin += "smin.";
                } else if (op->type.is_float()) {
                    intrin += "fmin.";
                } else {
                    intrin += "umin.";
                }
                intrin += patterns[i].op;
            }
            value = call_intrin(op->type, patterns[i].t.lanes(), intrin, {op->a, op->b});
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->type == Float(32)) {
        // Use a 2-wide vector instead
        Value *undef = UndefValue::get(f32x2);
        Constant *zero = ConstantInt::get(i32_t, 0);
        Value *a_wide = builder->CreateInsertElement(undef, codegen(op->a), zero);
        Value *b_wide = builder->CreateInsertElement(undef, codegen(op->b), zero);
        Value *wide_result;
        if (target.bits == 32) {
            wide_result = call_intrin(f32x2, 2, "llvm.arm.neon.vmaxs.v2f32", {a_wide, b_wide});
        } else {
            wide_result = call_intrin(f32x2, 2, "llvm.aarch64.neon.fmax.v2f32", {a_wide, b_wide});
        }
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "v8i8"},
        {UInt(16, 4), "v4i16"},
        {UInt(32, 2), "v2i32"},
        {Int(8, 8), "v8i8"},
        {Int(16, 4), "v4i16"},
        {Int(32, 2), "v2i32"},
        {Float(32, 2), "v2f32"},
        {UInt(8, 16), "v16i8"},
        {UInt(16, 8), "v8i16"},
        {UInt(32, 4), "v4i32"},
        {Int(8, 16), "v16i8"},
        {Int(16, 8), "v8i16"},
        {Int(32, 4), "v4i32"},
        {Float(32, 4), "v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        bool match = op->type == patterns[i].t;

        // The 128-bit versions are also used for other vector widths.
        if (op->type.is_vector() && patterns[i].t.lanes() * patterns[i].t.bits() == 128) {
            match = match || (op->type.element_of() == patterns[i].t.element_of());
        }

        if (match) {
            string intrin;
            if (target.bits == 32) {
                intrin = (string("llvm.arm.neon.") + (op->type.is_uint() ? "vmaxu." : "vmaxs.")) + patterns[i].op;
            } else {
                intrin = "llvm.aarch64.neon.";
                if (op->type.is_int()) {
                    intrin += "smax.";
                } else if (op->type.is_float()) {
                    intrin += "fmax.";
                } else {
                    intrin += "umax.";
                }
                intrin += patterns[i].op;
            }
            value = call_intrin(op->type, patterns[i].t.lanes(), intrin, {op->a, op->b});
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Store *op) {
    // Predicated store
    if (!is_one(op->predicate)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    // A dense store of an interleaving can be done using a vst2 intrinsic
    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp) {
        CodeGen_Posix::visit(op);
        return;
    }

    // First dig through let expressions
    Expr rhs = op->value;
    vector<pair<string, Expr>> lets;
    while (const Let *let = rhs.as<Let>()) {
        rhs = let->body;
        lets.push_back({ let->name, let->value });
    }
    const Shuffle *shuffle = rhs.as<Shuffle>();

    // Interleaving store instructions only exist for certain types.
    bool type_ok_for_vst = false;
    Type intrin_type = Handle();
    if (shuffle) {
        Type t = shuffle->vectors[0].type();
        intrin_type = t;
        Type elt = t.element_of();
        int vec_bits = t.bits() * t.lanes();
        if (elt == Float(32) ||
            elt == Int(8) || elt == Int(16) || elt == Int(32) ||
            elt == UInt(8) || elt == UInt(16) || elt == UInt(32)) {
            if (vec_bits % 128 == 0) {
                type_ok_for_vst = true;
                intrin_type = intrin_type.with_lanes(128 / t.bits());
            } else if (vec_bits % 64 == 0) {
                type_ok_for_vst = true;
                intrin_type = intrin_type.with_lanes(64 / t.bits());
            }
        }
    }

    if (is_one(ramp->stride) &&
        shuffle && shuffle->is_interleave() &&
        type_ok_for_vst &&
        2 <= shuffle->vectors.size() && shuffle->vectors.size() <= 4) {

        const int num_vecs = shuffle->vectors.size();
        vector<Value *> args(num_vecs);

        Type t = shuffle->vectors[0].type();

        // Assume element-aligned.
        int alignment = t.bytes();

        // Codegen the lets
        for (size_t i = 0; i < lets.size(); i++) {
            sym_push(lets[i].first, codegen(lets[i].second));
        }

        // Codegen all the vector args.
        for (int i = 0; i < num_vecs; ++i) {
            args[i] = codegen(shuffle->vectors[i]);
        }

        // Declare the function
        std::ostringstream instr;
        vector<llvm::Type *> arg_types;
        if (target.bits == 32) {
            instr << "llvm.arm.neon.vst"
                  << num_vecs
#if LLVM_VERSION > 37
                   << ".p0i8"
#endif
                  << ".v"
                  << intrin_type.lanes()
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits();
            arg_types = vector<llvm::Type *>(num_vecs + 2, llvm_type_of(intrin_type));
            arg_types.front() = i8_t->getPointerTo();
            arg_types.back() = i32_t;
        } else {
            instr << "llvm.aarch64.neon.st"
                  << num_vecs
                  << ".v"
                  << intrin_type.lanes()
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits()
                  << ".p0"
                  << (t.is_float() ? 'f' : 'i')
                  << t.bits();
            arg_types = vector<llvm::Type *>(num_vecs + 1, llvm_type_of(intrin_type));
            arg_types.back() = llvm_type_of(intrin_type.element_of())->getPointerTo();
        }
        llvm::FunctionType *fn_type = FunctionType::get(llvm::Type::getVoidTy(*context), arg_types, false);
        llvm::Function *fn = dyn_cast_or_null<llvm::Function>(module->getOrInsertFunction(instr.str(), fn_type));
        internal_assert(fn);

        // How many vst instructions do we need to generate?
        int slices = t.lanes() / intrin_type.lanes();

        internal_assert(slices >= 1);
        for (int i = 0; i < t.lanes(); i += intrin_type.lanes()) {
            Expr slice_base = simplify(ramp->base + i * num_vecs);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_type.lanes() * num_vecs);
            Value *ptr = codegen_buffer_pointer(op->name, shuffle->vectors[0].type().element_of(), slice_base);

            vector<Value *> slice_args = args;
            // Take a slice of each arg
            for (int j = 0; j < num_vecs ; j++) {
                slice_args[j] = slice_vector(slice_args[j], i, intrin_type.lanes());
            }

            if (target.bits == 32) {
                // The arm32 versions take an i8*, regardless of the type stored.
                ptr = builder->CreatePointerCast(ptr, i8_t->getPointerTo());
                // Set the pointer argument
                slice_args.insert(slice_args.begin(), ptr);
                // Set the alignment argument
                slice_args.push_back(ConstantInt::get(i32_t, alignment));
            } else {
                // Set the pointer argument
                slice_args.push_back(ptr);
            }

            CallInst *store = builder->CreateCall(fn, slice_args);
            add_tbaa_metadata(store, op->name, slice_ramp);
        }

        // pop the lets from the symbol table
        for (size_t i = 0; i < lets.size(); i++) {
            sym_pop(lets[i].first);
        }

        return;
    }

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    const IntImm *stride = ramp->stride.as<IntImm>();
    if (stride && (stride->value == 1 || stride->value == -1)) {
        CodeGen_Posix::visit(op);
        return;
    }

    // We have builtins for strided stores with fixed but unknown stride, but they use inline assembly
    if (target.bits != 64 /* Not yet implemented for aarch64 */) {
        ostringstream builtin;
        builtin << "strided_store_"
                << (op->value.type().is_float() ? 'f' : 'i')
                << op->value.type().bits()
                << 'x' << op->value.type().lanes();

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->value.type().element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->value.type().bytes());
            Value *val = codegen(op->value);
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Value *store_args[] = {base, stride, val};
            Instruction *store = builder->CreateCall(fn, store_args);
            (void)store;
            add_tbaa_metadata(store, op->name, op->index);
            return;
        }
    }

    CodeGen_Posix::visit(op);

}

void CodeGen_ARM::visit(const Load *op) {
    // Predicated load
    if (!is_one(op->predicate)) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    const Ramp *ramp = op->index.as<Ramp>();

    // We only deal with ramps here
    if (!ramp) {
        CodeGen_Posix::visit(op);
        return;
    }

    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : nullptr;

    // If the stride is one or minus one, we can deal with that using vanilla codegen
    if (stride && (stride->value == 1 || stride->value == -1)) {
        CodeGen_Posix::visit(op);
        return;
    }

    // Strided loads with known stride
    if (stride && stride->value >= 2 && stride->value <= 4) {
        // Check alignment on the base. Attempt to shift to an earlier
        // address if it simplifies the expression. This makes
        // adjacent strided loads shared a vldN op.
        Expr base = ramp->base;
        int offset = 0;
        ModulusRemainder mod_rem = modulus_remainder(ramp->base);

        const Add *add = base.as<Add>();
        const IntImm *add_b = add ? add->b.as<IntImm>() : nullptr;

        if ((mod_rem.modulus % stride->value) == 0) {
            offset = mod_rem.remainder % stride->value;
        } else if ((mod_rem.modulus == 1) && add_b) {
            offset = add_b->value % stride->value;
            if (offset < 0) {
                offset += stride->value;
            }
        }

        if (offset) {
            base = simplify(base - offset);
            mod_rem.remainder -= offset;
            if (mod_rem.modulus) {
                mod_rem.remainder = mod_imp(mod_rem.remainder, mod_rem.modulus);
            }
        }

        int alignment = op->type.bytes();
        alignment *= gcd(mod_rem.modulus, mod_rem.remainder);
        // Maximum stack alignment on arm is 16 bytes, so we should
        // never claim alignment greater than that.
        alignment = gcd(alignment, 16);
        internal_assert(alignment > 0);

        // Decide what width to slice things into. If not a multiple
        // of 64 or 128 bits, then we can't safely slice it up into
        // some number of vlds, so we hand it over the base class.
        int bit_width = op->type.bits() * op->type.lanes();
        int intrin_lanes = 0;
        if (bit_width % 128 == 0) {
            intrin_lanes = 128 / op->type.bits();
        } else if (bit_width % 64 == 0) {
            intrin_lanes = 64 / op->type.bits();
        } else {
            CodeGen_Posix::visit(op);
            return;
        }

        llvm::Type *load_return_type = llvm_type_of(op->type.with_lanes(intrin_lanes*stride->value));
        llvm::Type *load_return_pointer_type = load_return_type->getPointerTo();
        Value *undef = UndefValue::get(load_return_type);
        SmallVector<Constant*, 256> constants;
        for(int j = 0; j < intrin_lanes; j++) {
            Constant *constant = ConstantInt::get(i32_t, j*stride->value+offset);
            constants.push_back(constant);
        }
        Constant* constantsV = ConstantVector::get(constants);

        vector<Value *> results;
        for (int i = 0; i < op->type.lanes(); i += intrin_lanes) {
            Expr slice_base = simplify(base + i*ramp->stride);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_lanes);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
            Value *bitcastI = builder->CreateBitOrPointerCast(ptr, load_return_pointer_type);
            LoadInst *loadI = cast<LoadInst>(builder->CreateLoad(bitcastI));
            loadI->setAlignment(alignment);
            add_tbaa_metadata(loadI, op->name, slice_ramp);
            Value *shuffleInstr = builder->CreateShuffleVector(loadI, undef, constantsV);
            results.push_back(shuffleInstr);
        }

        // Concat the results
        value = concat_vectors(results);
        return;
    }

    // We have builtins for strided loads with fixed but unknown stride, but they use inline assembly.
    if (target.bits != 64 /* Not yet implemented for aarch64 */) {
        ostringstream builtin;
        builtin << "strided_load_"
                << (op->type.is_float() ? 'f' : 'i')
                << op->type.bits()
                << 'x' << op->type.lanes();

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->type.bytes());
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Value *args[] = {base, stride};
            Instruction *load = builder->CreateCall(fn, args, builtin.str());
            add_tbaa_metadata(load, op->name, op->index);
            value = load;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Call *op) {
    if (op->is_intrinsic(Call::abs) && op->type.is_uint()) {
        internal_assert(op->args.size() == 1);
        // If the arg is a subtract with narrowable args, we can use vabdl.
        const Sub *sub = op->args[0].as<Sub>();
        if (sub) {
            Expr a = sub->a, b = sub->b;
            Type narrow = UInt(a.type().bits()/2, a.type().lanes());
            Expr na = lossless_cast(narrow, a);
            Expr nb = lossless_cast(narrow, b);

            // Also try an unsigned narrowing
            if (!na.defined() || !nb.defined()) {
                narrow = Int(narrow.bits(), narrow.lanes());
                na = lossless_cast(narrow, a);
                nb = lossless_cast(narrow, b);
            }

            if (na.defined() && nb.defined()) {
                Expr absd = Call::make(UInt(narrow.bits(), narrow.lanes()), Call::absd,
                                       {na, nb}, Call::PureIntrinsic);

                absd = Cast::make(op->type, absd);
                codegen(absd);
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

string CodeGen_ARM::mcpu() const {
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            return "swift";
        } else {
            return "cortex-a9";
        }
    } else {
        if (target.os == Target::IOS) {
            return "cyclone";
        } else {
            return "generic";
        }
    }
}

string CodeGen_ARM::mattrs() const {
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            return "+neon";
        } if (!target.has_feature(Target::NoNEON)) {
            return "+neon";
        } else {
            return "-neon";
        }
    } else {
        if (target.os == Target::IOS || target.os == Target::OSX) {
            return "+reserve-x18";
        } else {
            return "";
        }
    }
}

bool CodeGen_ARM::use_soft_float_abi() const {
    // One expects the flag is irrelevant on 64-bit, but we'll make the logic
    // exhaustive anyway. It is not clear the armv7s case is necessary either.
    return target.has_feature(Target::SoftFloatABI) ||
        (target.bits == 32 &&
         ((target.os == Target::Android) ||
          (target.os == Target::IOS && !target.has_feature(Target::ARMv7s))));
}

int CodeGen_ARM::native_vector_bits() const {
    return 128;
}

}}

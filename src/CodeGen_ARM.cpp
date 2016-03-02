#include <iostream>
#include <sstream>

#include "CodeGen_ARM.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IntegerDivisionTable.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;
using std::pair;
using std::make_pair;

using namespace llvm;

namespace {
// cast operators
Expr _i64(Expr e) {
    return cast(Int(64, e.type().lanes()), e);
}

Expr _u64(Expr e) {
    return cast(UInt(64, e.type().lanes()), e);
}
Expr _i32(Expr e) {
    return cast(Int(32, e.type().lanes()), e);
}

Expr _u32(Expr e) {
    return cast(UInt(32, e.type().lanes()), e);
}

Expr _i16(Expr e) {
    return cast(Int(16, e.type().lanes()), e);
}

Expr _u16(Expr e) {
    return cast(UInt(16, e.type().lanes()), e);
}

/*
Expr _f32(Expr e) {
    return cast(Float(32, e.type().lanes()), e);
}

Expr _f64(Expr e) {
    return cast(Float(64, e.type().lanes()), e);
}
*/

// saturating cast operators
Expr _i8q(Expr e) {
    return cast(Int(8, e.type().lanes()), clamp(e, -128, 127));
}

Expr _u8q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(8, e.type().lanes()), min(e, 255));
    } else {
        return cast(UInt(8, e.type().lanes()), clamp(e, 0, 255));
    }
}

Expr _i16q(Expr e) {
    return cast(Int(16, e.type().lanes()), clamp(e, -32768, 32767));
}

Expr _u16q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(16, e.type().lanes()), min(e, 65535));
    } else {
        return cast(UInt(16, e.type().lanes()), clamp(e, 0, 65535));
    }
}

Expr _i32q(Expr e) {
    return cast(Int(32, e.type().lanes()), clamp(e, Int(32).min(), Int(32).max()));
}

Expr _u32q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(32, e.type().lanes()), min(e, UInt(32).max()));
    } else {
        return cast(UInt(32, e.type().lanes()), clamp(e, 0, UInt(32).max()));
    }
}
}

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

    #if !(WITH_NATIVE_CLIENT)
    user_assert(target.os != Target::NaCl) << "llvm build not configured with native client enabled\n.";
    #endif

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

    casts.push_back(Pattern("vqshiftns.v8i8",  "sqshrn.v8i8",  8, _i8q(wild_i16x_/wild_i16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftns.v4i16", "sqshrn.v4i16", 4, _i16q(wild_i32x_/wild_i32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftns.v2i32", "sqshrn.v2i32", 2, _i32q(wild_i64x_/wild_i64x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v8i8",  "uqshrn.v8i8",  8, _u8q(wild_u16x_/wild_u16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v4i16", "uqshrn.v4i16", 4, _u16q(wild_u32x_/wild_u32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnu.v2i32", "uqshrn.v2i32", 2, _u32q(wild_u64x_/wild_u64x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v8i8",  "sqshrun.v8i8",  8, _u8q(wild_i16x_/wild_i16x_),  Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v4i16", "sqshrun.v4i16", 4, _u16q(wild_i32x_/wild_i32x_), Pattern::RightShift));
    casts.push_back(Pattern("vqshiftnsu.v2i32", "sqshrun.v2i32", 2, _u32q(wild_i64x_/wild_i64x_), Pattern::RightShift));

    // Where a 64-bit and 128-bit version exist, we use the 64-bit
    // version only when the args are 64-bits wide.
    casts.push_back(Pattern("vqshifts.v8i8",  "sqshl.v8i8",  8, _i8q(_i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v4i16", "sqshl.v4i16", 4, _i16q(_i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v2i32", "sqshl.v2i32", 2, _i32q(_i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v8i8",  "uqshl.v8i8",  8, _u8q(_u16(wild_u8x8)*wild_u16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v4i16", "uqshl.v4i16", 4, _u16q(_u32(wild_u16x4)*wild_u32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v2i32", "uqshl.v2i32", 2, _u32q(_u64(wild_u32x2)*wild_u64x2), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v8i8",  "sqshlu.v8i8",  8, _u8q(_i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v4i16", "sqshlu.v4i16", 4, _u16q(_i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v2i32", "sqshlu.v2i32", 2, _u32q(_i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));

    // We use the 128-bit version for all other vector widths.
    casts.push_back(Pattern("vqshifts.v16i8", "sqshl.v16i8", 16, _i8q(_i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v8i16", "sqshl.v8i16",  8, _i16q(_i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshifts.v4i32", "sqshl.v4i32",  4, _i32q(_i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v16i8", "uqshl.v16i8",  16, _u8q(_u16(wild_u8x_)*wild_u16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v8i16", "uqshl.v8i16",  8, _u16q(_u32(wild_u16x_)*wild_u32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftu.v4i32", "uqshl.v4i32",  4, _u32q(_u64(wild_u32x_)*wild_u64x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v16i8", "sqshlu.v16i8", 16, _u8q(_i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v8i16", "sqshlu.v8i16", 8, _u16q(_i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
    casts.push_back(Pattern("vqshiftsu.v4i32", "sqshlu.v4i32", 4, _u32q(_i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));

    casts.push_back(Pattern("vqmovns.v8i8",  "sqxtn.v8i8",    8,  _i8q(wild_i16x_)));
    casts.push_back(Pattern("vqmovns.v4i16", "sqxtn.v4i16",   4, _i16q(wild_i32x_)));
    casts.push_back(Pattern("vqmovns.v2i32", "sqxtn.v2i32",   2, _i32q(wild_i64x_)));
    casts.push_back(Pattern("vqmovnu.v8i8",  "uqxtn.v8i8",    8,  _u8q(wild_u16x_)));
    casts.push_back(Pattern("vqmovnu.v4i16", "uqxtn.v4i16",   4, _u16q(wild_u32x_)));
    casts.push_back(Pattern("vqmovnu.v2i32", "uqxtn.v2i32",   2, _u32q(wild_u64x_)));
    casts.push_back(Pattern("vqmovnsu.v8i8",  "sqxtun.v8i8",  8,  _u8q(wild_i16x_)));
    casts.push_back(Pattern("vqmovnsu.v4i16", "sqxtun.v4i16", 4, _u16q(wild_i32x_)));
    casts.push_back(Pattern("vqmovnsu.v2i32", "sqxtun.v2i32", 2, _u32q(wild_i64x_)));

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
                    Value *shift = NULL;
                    // The arm64 llvm backend wants i32 constants for right shifts.
                    if (target.bits == 64 && pattern.type == Pattern::RightShift) {
                        shift = ConstantInt::get(i32, shift_amount);
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
        if (d && is_const(d->b, 1 << t.bits())) {
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
        if (c && c->name == Call::absd && c->call_type == Call::Intrinsic) {
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
                Value *shift = NULL;
                if (target.bits == 32) {
                    shift = ConstantInt::get(t_arg, shift_amount);
                } else {
                    shift = ConstantInt::get(i32, shift_amount);
                }
                value = call_pattern(pattern, t_result,
                                     {codegen(matches[0]), shift});
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Div *op) {
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->type.is_vector() && is_two(op->b) &&
        (op->a.as<Add>() || op->a.as<Sub>())) {
        vector<Expr> matches;
        for (size_t i = 0; i < averagings.size(); i++) {
            if (expr_match(averagings[i].pattern, op->a, matches)) {
                value = call_pattern(averagings[i], op->type, matches);
                return;
            }
        }
    }

    // Detect if it's a small int division
    const int64_t *const_int_divisor = as_const_int(op->b);
    const uint64_t *const_uint_divisor = as_const_uint(op->b);

    // Check if the divisor is a power of two
    int shift_amount;
    bool power_of_two = is_const_power_of_two_integer(op->b, &shift_amount);

    vector<Expr> matches;
    if (power_of_two && op->type.is_int()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateAShr(numerator, shift);
    } else if (power_of_two && op->type.is_uint()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateLShr(numerator, shift);
    } else if (const_int_divisor &&
               op->type.is_int() &&
               (op->type.bits() == 32 || op->type.bits() == 16 || op->type.bits() == 8) &&
               *const_int_divisor > 1 &&
               ((op->type.bits() > 8 && *const_int_divisor < 256) || *const_int_divisor < 128)) {

        int64_t multiplier, shift;
        if (op->type.bits() == 32) {
            multiplier = IntegerDivision::table_s32[*const_int_divisor][2];
            shift      = IntegerDivision::table_s32[*const_int_divisor][3];
        } else if (op->type.bits() == 16) {
            multiplier = IntegerDivision::table_s16[*const_int_divisor][2];
            shift      = IntegerDivision::table_s16[*const_int_divisor][3];
        } else {
            // 8 bit
            multiplier = IntegerDivision::table_s8[*const_int_divisor][2];
            shift      = IntegerDivision::table_s8[*const_int_divisor][3];
        }

        Value *val = codegen(op->a);

        // Make an all-ones mask if the numerator is negative
        Value *sign = builder->CreateAShr(val, codegen(make_const(op->type, op->type.bits()-1)));
        // Flip the numerator bits if the mask is high
        Value *flipped = builder->CreateXor(sign, val);
        // Grab the multiplier
        Value *mult = codegen(make_const(op->type, (int)multiplier));
        // Widening multiply
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(Int(op->type.bits()*2, op->type.lanes()));
        // flipped's high bit is zero, so it's ok to zero-extend it
        Value *flipped_wide = builder->CreateIntCast(flipped, wider, false);
        Value *mult_wide = builder->CreateIntCast(mult, wider, false);
        Value *wide_val = builder->CreateMul(flipped_wide, mult_wide);
        // Do the shift (add 8 or 16 to narrow back down)
        Constant *shift_amount = ConstantInt::get(wider, (shift + op->type.bits()));
        val = builder->CreateLShr(wide_val, shift_amount);
        val = builder->CreateIntCast(val, narrower, true);
        // Maybe flip the bits again
        value = builder->CreateXor(val, sign);

    } else if (const_uint_divisor &&
               op->type.is_uint() &&
               (op->type.bits() == 32 || op->type.bits() == 16 || op->type.bits() == 8) &&
               *const_uint_divisor > 1 && *const_uint_divisor < 256) {

        int64_t method, multiplier, shift;
        if (op->type.bits() == 32) {
            method     = IntegerDivision::table_u32[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u32[*const_uint_divisor][2];
            shift      = IntegerDivision::table_u32[*const_uint_divisor][3];
        } else if (op->type.bits() == 16) {
            method     = IntegerDivision::table_u16[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u16[*const_uint_divisor][2];
            shift      = IntegerDivision::table_u16[*const_uint_divisor][3];
        } else {
            method     = IntegerDivision::table_u8[*const_uint_divisor][1];
            multiplier = IntegerDivision::table_u8[*const_uint_divisor][2];
            shift      = IntegerDivision::table_u8[*const_uint_divisor][3];
        }

        internal_assert(method != 0)
            << "method 0 division is for powers of two and should have been handled elsewhere\n";

        Value *num = codegen(op->a);

        // Widen
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(UInt(op->type.bits()*2, op->type.lanes()));
        Value *mult = ConstantInt::get(narrower, multiplier);
        mult = builder->CreateIntCast(mult, wider, false);
        Value *val = builder->CreateIntCast(num, wider, false);

        // Multiply
        val = builder->CreateMul(val, mult);

        // Narrow
        int shift_bits = op->type.bits();
        // For method 1, we can do the final shift here too.
        if (method == 1) {
            shift_bits += (int)shift;
        }
        Constant *shift_amount = ConstantInt::get(wider, shift_bits);
        val = builder->CreateLShr(val, shift_amount);
        val = builder->CreateIntCast(val, narrower, false);

        // Average with original numerator
        if (method == 2) {
            if (op->type.is_vector() && op->type.bits() == 32) {
                if (target.bits == 32) {
                    val = call_intrin(narrower, 2, "llvm.arm.neon.vhaddu.v2i32", {val, num});
                } else {
                    val = call_intrin(narrower, 2, "llvm.aarch64.neon.uhadd.v2i32", {val, num});
                }
            } else if (op->type.is_vector() && op->type.bits() == 16) {
                if (target.bits == 32) {
                    val = call_intrin(narrower, 4, "llvm.arm.neon.vhaddu.v4i16", {val, num});
                } else {
                    val = call_intrin(narrower, 4, "llvm.aarch64.neon.uhadd.v4i16", {val, num});
                }
            } else if (op->type.is_vector() && op->type.bits() == 8) {
                if (target.bits == 32) {
                    val = call_intrin(narrower, 8, "llvm.arm.neon.vhaddu.v8i8", {val, num});
                } else {
                    val = call_intrin(narrower, 8, "llvm.aarch64.neon.uhadd.v8i8", {val, num});
                }
            } else {
                // num > val, so the following works without widening:
                // val += (num - val)/2
                Value *diff = builder->CreateSub(num, val);
                diff = builder->CreateLShr(diff, ConstantInt::get(diff->getType(), 1));
                val = builder->CreateAdd(val, diff);
            }

            // Do the final shift
            if (shift) {
                val = builder->CreateLShr(val, ConstantInt::get(narrower, shift));
            }
        }

        value = val;
    } else {
        CodeGen_Posix::visit(op);
    }
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
            a = ConstantFP::getNegativeZero(f32);
        } else if (op->type.bits() == 64) {
            a = ConstantFP::getNegativeZero(f64);
        } else {
            a = NULL;
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
        Constant *zero = ConstantInt::get(i32, 0);
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
        Constant *zero = ConstantInt::get(i32, 0);
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
        lets.push_back(make_pair(let->name, let->value));
    }
    const Call *call = rhs.as<Call>();

    // Interleaving store instructions only exist for certain types.
    bool type_ok_for_vst = false;
    Type intrin_type = Handle();
    if (call && !call->args.empty()) {
        Type t = call->args[0].type();
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
        call && call->call_type == Call::Intrinsic &&
        call->name == Call::interleave_vectors &&
        type_ok_for_vst &&
        2 <= call->args.size() && call->args.size() <= 4) {

        const int num_vecs = call->args.size();
        vector<Value *> args(num_vecs);

        Type t = call->args[0].type();

        // Assume element-aligned.
        int alignment = t.bytes();

        // Codegen the lets
        for (size_t i = 0; i < lets.size(); i++) {
            sym_push(lets[i].first, codegen(lets[i].second));
        }

        // Codegen all the vector args.
        for (int i = 0; i < num_vecs; ++i) {
            args[i] = codegen(call->args[i]);
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
            arg_types.front() = i8->getPointerTo();
            arg_types.back() = i32;
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
            Value *ptr = codegen_buffer_pointer(op->name, call->args[0].type().element_of(), slice_base);

            vector<Value *> slice_args = args;
            // Take a slice of each arg
            for (int j = 0; j < num_vecs ; j++) {
                slice_args[j] = slice_vector(slice_args[j], i, intrin_type.lanes());
            }

            if (target.bits == 32) {
                // The arm32 versions take an i8*, regardless of the type stored.
                ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
                // Set the pointer argument
                slice_args.insert(slice_args.begin(), ptr);
                // Set the alignment argument
                slice_args.push_back(ConstantInt::get(i32, alignment));
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
    if (target.os != Target::NaCl /* No inline assembly in NaCl */ &&
        target.bits != 64 /* Not yet implemented for aarch64 */) {
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

    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;

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
        const IntImm *add_b = add ? add->b.as<IntImm>() : NULL;

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
        alignment *= gcd(gcd(mod_rem.modulus, mod_rem.remainder), 16);
        internal_assert(alignment > 0);

        Value *align = ConstantInt::get(i32, alignment);

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

        // Declare the intrinsic
        ostringstream intrin;
        llvm::FunctionType *fn_type;
        llvm::Type *return_type;
        {
            llvm::Type *one_vec = llvm_type_of(op->type.with_lanes(intrin_lanes));
            vector<llvm::Type *> elements(stride->value, one_vec);
            return_type = StructType::get(*context, elements);
        }
        if (target.bits == 32) {
            intrin << "llvm.arm.neon.vld"
                   << stride->value
                   << ".v" << intrin_lanes
                   << (op->type.is_float() ? 'f' : 'i')
                   << op->type.bits()
#if LLVM_VERSION > 37
                   << ".p0i8"
#endif
                   ;
            // The intrinsic takes an i8 pointer and an alignment, and
            // returns a struct of stride->value vectors of width
            // intrin_lanes.
            llvm::Type *arg_types[] = {i8->getPointerTo(), i32};
            fn_type = llvm::FunctionType::get(return_type, arg_types, false);
        } else {
            intrin << "llvm.aarch64.neon.ld"
                   << stride->value
                   << ".v" << intrin_lanes
                   << (op->type.is_float() ? 'f' : 'i')
                   << op->type.bits()
                   << ".p0"
                   << (op->type.is_float() ? 'f' : 'i')
                   << op->type.bits();
            // The intrinsic takes a pointer to the element type and
            // returns a struct of stride->value vectors of width
            // intrin_lanes.
            llvm::Type *arg_type = llvm_type_of(op->type.element_of())->getPointerTo();
            llvm::Type *arg_types[] = {arg_type};
            fn_type = llvm::FunctionType::get(return_type, arg_types, false);
        }

        // Get the intrinsic
        llvm::Function *fn = dyn_cast_or_null<llvm::Function>(module->getOrInsertFunction(intrin.str(), fn_type));
        internal_assert(fn);

        // Load each slice.
        vector<Value *> results;
        for (int i = 0; i < op->type.lanes(); i += intrin_lanes) {
            Expr slice_base = simplify(base + i*ramp->stride);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_lanes);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
            CallInst *call = NULL;
            if (target.bits == 32) {
                // The arm32 versions always take an i8* pointer.
                ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
                Value *args[] = {ptr, align};
                call = builder->CreateCall(fn, args);
            } else {
                // The aarch64 versions allow arbitrary alignment.
                Value *args[] = {ptr};
                call = builder->CreateCall(fn, args);
            }
            add_tbaa_metadata(call, op->name, slice_ramp);

            Value *elt = builder->CreateExtractValue(call, {(unsigned int)offset});
            results.push_back(elt);
        }

        // Concat the results
        value = concat_vectors(results);
        return;
    }

    // We have builtins for strided loads with fixed but unknown stride, but they use inline assembly.
    if (target.os != Target::NaCl /* No inline assembly in nacl */ &&
        target.bits != 64 /* Not yet implemented for aarch64 */) {
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
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::abs && op->type.is_uint()) {
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
                                           {na, nb}, Call::Intrinsic);

                    absd = Cast::make(op->type, absd);
                    codegen(absd);
                    return;
                }
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
    return target.bits == 32 &&
        ((target.os == Target::Android) ||
         (target.os == Target::IOS && !target.has_feature(Target::ARMv7s)));
}

int CodeGen_ARM::native_vector_bits() const {
    return 128;
}

}}

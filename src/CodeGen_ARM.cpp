#include <iostream>
#include <sstream>

#include "CSE.h"
#include "CodeGen_ARM.h"
#include "CodeGen_Internal.h"
#include "ConciseCasts.h"
#include "Debug.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "Simplify.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::ostringstream;
using std::pair;
using std::string;
using std::vector;

using namespace Halide::ConciseCasts;
using namespace llvm;

CodeGen_ARM::CodeGen_ARM(Target target)
    : CodeGen_Posix(target) {
    if (target.bits == 32) {
#if !defined(WITH_ARM)
        user_error << "arm not enabled for this build of Halide.";
#endif
        user_assert(llvm_ARM_enabled) << "llvm build not configured with ARM target enabled\n.";
    } else {
#if !defined(WITH_AARCH64)
        user_error << "aarch64 not enabled for this build of Halide.";
#endif
        user_assert(llvm_AArch64_enabled) << "llvm build not configured with AArch64 target enabled.\n";
    }

    casts.emplace_back("vqrdmulh.v4i16", "sqrdmulh.v4i16", 4,
                       i16_sat(rounding_shift_right(widening_mul(wild_i16x4, wild_i16x4), 15)));
    casts.emplace_back("vqrdmulh.v8i16", "sqrdmulh.v8i16", 8,
                       i16_sat(rounding_shift_right(widening_mul(wild_i16x_, wild_i16x_), 15)));
    casts.emplace_back("vqrdmulh.v2i32", "sqrdmulh.v2i32", 2,
                       i32_sat(rounding_shift_right(widening_mul(wild_i32x2, wild_i32x2), 31)));
    casts.emplace_back("vqrdmulh.v4i32", "sqrdmulh.v4i32", 4,
                       i32_sat(rounding_shift_right(widening_mul(wild_i32x_, wild_i32x_), 31)));

    casts.emplace_back("vqshiftns.v8i8", "sqshrn.v8i8", 8, i8_sat(wild_i16x_ >> wild_u16_));
    casts.emplace_back("vqshiftns.v4i16", "sqshrn.v4i16", 4, i16_sat(wild_i32x_ >> wild_u32_));
    casts.emplace_back("vqshiftns.v2i32", "sqshrn.v2i32", 2, i32_sat(wild_i64x_ >> wild_u64_));
    casts.emplace_back("vqshiftnu.v8i8", "uqshrn.v8i8", 8, u8_sat(wild_u16x_ >> wild_u16_));
    casts.emplace_back("vqshiftnu.v4i16", "uqshrn.v4i16", 4, u16_sat(wild_u32x_ >> wild_u32_));
    casts.emplace_back("vqshiftnu.v2i32", "uqshrn.v2i32", 2, u32_sat(wild_u64x_ >> wild_u64_));
    casts.emplace_back("vqshiftnsu.v8i8", "sqshrun.v8i8", 8, u8_sat(wild_i16x_ >> wild_u16_));
    casts.emplace_back("vqshiftnsu.v4i16", "sqshrun.v4i16", 4, u16_sat(wild_i32x_ >> wild_u32_));
    casts.emplace_back("vqshiftnsu.v2i32", "sqshrun.v2i32", 2, u32_sat(wild_i64x_ >> wild_u64_));

    casts.emplace_back("vqrshiftns.v8i8", "sqrshrn.v8i8", 8, i8_sat(rounding_shift_right(wild_i16x_, wild_u32_)));
    casts.emplace_back("vqrshiftns.v4i16", "sqrshrn.v4i16", 4, i16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("vqrshiftns.v2i32", "sqrshrn.v2i32", 2, i32_sat(rounding_shift_right(wild_i64x_, wild_u32_)));
    casts.emplace_back("vqrshiftnu.v8i8", "uqrshrn.v8i8", 8, u8_sat(rounding_shift_right(wild_u16x_, wild_u32_)));
    casts.emplace_back("vqrshiftnu.v4i16", "uqrshrn.v4i16", 4, u16_sat(rounding_shift_right(wild_u32x_, wild_u32_)));
    casts.emplace_back("vqrshiftnu.v2i32", "uqrshrn.v2i32", 2, u32_sat(rounding_shift_right(wild_u64x_, wild_u32_)));
    casts.emplace_back("vqrshiftnsu.v8i8", "sqrshrun.v8i8", 8, u8_sat(rounding_shift_right(wild_i16x_, wild_u32_)));
    casts.emplace_back("vqrshiftnsu.v4i16", "sqrshrun.v4i16", 4, u16_sat(rounding_shift_right(wild_i32x_, wild_u32_)));
    casts.emplace_back("vqrshiftnsu.v2i32", "sqrshrun.v2i32", 2, u32_sat(rounding_shift_right(wild_i64x_, wild_u32_)));

    // Where a 64-bit and 128-bit version exist, we use the 64-bit
    // version only when the args are 64-bits wide.
    casts.emplace_back("vqshifts.v8i8", "sqshl.v8i8", 8, i8_sat(widening_shift_left(wild_i8x8, wild_i8x8)));
    casts.emplace_back("vqshifts.v4i16", "sqshl.v4i16", 4, i16_sat(widening_shift_left(wild_i16x4, wild_i16x4)));
    casts.emplace_back("vqshifts.v2i32", "sqshl.v2i32", 2, i32_sat(widening_shift_left(wild_i32x2, wild_i32x2)));
    casts.emplace_back("vqshiftu.v8i8", "uqshl.v8i8", 8, u8_sat(widening_shift_left(wild_u8x8, wild_i8x8)));
    casts.emplace_back("vqshiftu.v4i16", "uqshl.v4i16", 4, u16_sat(widening_shift_left(wild_u16x4, wild_i16x4)));
    casts.emplace_back("vqshiftu.v2i32", "uqshl.v2i32", 2, u32_sat(widening_shift_left(wild_u32x2, wild_i32x2)));
    casts.emplace_back("vqshiftsu.v8i8", "sqshlu.v8i8", 8, u8_sat(widening_shift_left(wild_i8x8, wild_i8x8)));
    casts.emplace_back("vqshiftsu.v4i16", "sqshlu.v4i16", 4, u16_sat(widening_shift_left(wild_i16x4, wild_i16x4)));
    casts.emplace_back("vqshiftsu.v2i32", "sqshlu.v2i32", 2, u32_sat(widening_shift_left(wild_i32x2, wild_i32x2)));

    // We use the 128-bit version for all other vector widths.
    casts.emplace_back("vqshifts.v16i8", "sqshl.v16i8", 16, i8_sat(widening_shift_left(wild_i8x_, wild_i8x_)));
    casts.emplace_back("vqshifts.v8i16", "sqshl.v8i16", 8, i16_sat(widening_shift_left(wild_i16x_, wild_i16x_)));
    casts.emplace_back("vqshifts.v4i32", "sqshl.v4i32", 4, i32_sat(widening_shift_left(wild_i32x_, wild_i32x_)));
    casts.emplace_back("vqshiftu.v16i8", "uqshl.v16i8", 16, u8_sat(widening_shift_left(wild_u8x_, wild_i8x_)));
    casts.emplace_back("vqshiftu.v8i16", "uqshl.v8i16", 8, u16_sat(widening_shift_left(wild_u16x_, wild_i16x_)));
    casts.emplace_back("vqshiftu.v4i32", "uqshl.v4i32", 4, u32_sat(widening_shift_left(wild_u32x_, wild_i32x_)));
    casts.emplace_back("vqshiftsu.v16i8", "sqshlu.v16i8", 16, u8_sat(widening_shift_left(wild_i8x_, wild_i8x_)));
    casts.emplace_back("vqshiftsu.v8i16", "sqshlu.v8i16", 8, u16_sat(widening_shift_left(wild_i16x_, wild_i16x_)));
    casts.emplace_back("vqshiftsu.v4i32", "sqshlu.v4i32", 4, u32_sat(widening_shift_left(wild_i32x_, wild_i32x_)));

    casts.emplace_back("vqmovns.v8i8", "sqxtn.v8i8", 8, i8_sat(wild_i16x_));
    casts.emplace_back("vqmovns.v4i16", "sqxtn.v4i16", 4, i16_sat(wild_i32x_));
    casts.emplace_back("vqmovns.v2i32", "sqxtn.v2i32", 2, i32_sat(wild_i64x_));
    casts.emplace_back("vqmovnu.v8i8", "uqxtn.v8i8", 8, u8_sat(wild_u16x_));
    casts.emplace_back("vqmovnu.v4i16", "uqxtn.v4i16", 4, u16_sat(wild_u32x_));
    casts.emplace_back("vqmovnu.v2i32", "uqxtn.v2i32", 2, u32_sat(wild_u64x_));
    casts.emplace_back("vqmovnsu.v8i8", "sqxtun.v8i8", 8, u8_sat(wild_i16x_));
    casts.emplace_back("vqmovnsu.v4i16", "sqxtun.v4i16", 4, u16_sat(wild_i32x_));
    casts.emplace_back("vqmovnsu.v2i32", "sqxtun.v2i32", 2, u32_sat(wild_i64x_));

    // 64-bit saturating negation
    negations.emplace_back("vqneg.v8i8", "sqneg.v8i8", 8, -max(wild_i8x8, -127));
    negations.emplace_back("vqneg.v4i16", "sqneg.v4i16", 4, -max(wild_i16x4, -32767));
    negations.emplace_back("vqneg.v2i32", "sqneg.v2i32", 2, -max(wild_i32x2, -(0x7fffffff)));

    // 128-bit
    negations.emplace_back("vqneg.v16i8", "sqneg.v16i8", 16, -max(wild_i8x_, -127));
    negations.emplace_back("vqneg.v8i16", "sqneg.v8i16", 8, -max(wild_i16x_, -32767));
    negations.emplace_back("vqneg.v4i32", "sqneg.v4i32", 4, -max(wild_i32x_, -(0x7fffffff)));
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

    for (size_t i = 0; i < casts.size(); i++) {
        const Pattern &pattern = casts[i];
        //debug(4) << "Trying pattern: " << patterns[i].intrin << " " << patterns[i].pattern << "\n";
        if (expr_match(pattern.pattern, op, matches)) {

            //debug(4) << "Match!\n";
            if (pattern.type == Pattern::Simple) {
                value = call_pattern(pattern, t, matches);
                return;
            } else {  // must be a shift
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
        if (const Call *absd = Call::as_intrinsic(op->value, {Call::absd})) {
            ostringstream ss;
            int intrin_lanes = 128 / t.bits();
            ss << "vabdl_" << (absd->args[0].type().is_int() ? "i" : "u") << t.bits() / 2 << "x" << intrin_lanes;
            value = call_intrin(t, intrin_lanes, ss.str(), absd->args);
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

    vector<Expr> matches;

    int shift_amount = 0;
    if (is_const_power_of_two_integer(op->b, &shift_amount)) {
        // Let LLVM handle these.
        CodeGen_Posix::visit(op);
        return;
    }

    // Vector multiplies by 3, 5, 7, 9 should do shift-and-add or
    // shift-and-sub instead to reduce register pressure (the
    // shift is an immediate)
    // TODO: Verify this is still good codegen.
    if (is_const(op->b, 3)) {
        value = codegen(op->a * 2 + op->a);
        return;
    } else if (is_const(op->b, 5)) {
        value = codegen(op->a * 4 + op->a);
        return;
    } else if (is_const(op->b, 7)) {
        value = codegen(op->a * 8 - op->a);
        return;
    } else if (is_const(op->b, 9)) {
        value = codegen(op->a * 8 + op->a);
        return;
    }

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
    if (op->type.is_float() &&
        op->type.bits() >= 32 &&
        is_const_zero(op->a)) {
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
            a = ConstantVector::getSplat(element_count(op->type.lanes()), a);
        }
        value = builder->CreateFSub(a, b);
        return;
    }

    CodeGen_Posix::visit(op);
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
        Value *a = codegen(op->a);
        Value *a_wide = builder->CreateInsertElement(undef, a, zero);
        Value *b = codegen(op->b);
        Value *b_wide = builder->CreateInsertElement(undef, b, zero);
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
        {Float(32, 4), "v4f32"}};

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
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
        Value *a = codegen(op->a);
        Value *a_wide = builder->CreateInsertElement(undef, a, zero);
        Value *b = codegen(op->b);
        Value *b_wide = builder->CreateInsertElement(undef, b, zero);
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
        {Float(32, 4), "v4f32"}};

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
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
    if (!is_const_one(op->predicate)) {
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
        lets.emplace_back(let->name, let->value);
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

    if (is_const_one(ramp->stride) &&
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
                  << ".p0i8"
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
        llvm::FunctionCallee fn = module->getOrInsertFunction(instr.str(), fn_type);
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
            for (int j = 0; j < num_vecs; j++) {
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
                << (op->value.type().is_float() ? "f" : "i")
                << op->value.type().bits()
                << "x" << op->value.type().lanes();

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
    if (!is_const_one(op->predicate)) {
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

        llvm::Type *load_return_type = llvm_type_of(op->type.with_lanes(intrin_lanes * stride->value));
        llvm::Type *load_return_pointer_type = load_return_type->getPointerTo();
        Value *undef = UndefValue::get(load_return_type);
        SmallVector<Constant *, 256> constants;
        for (int j = 0; j < intrin_lanes; j++) {
            Constant *constant = ConstantInt::get(i32_t, j * stride->value + offset);
            constants.push_back(constant);
        }
        Constant *constantsV = ConstantVector::get(constants);

        vector<Value *> results;
        for (int i = 0; i < op->type.lanes(); i += intrin_lanes) {
            Expr slice_base = simplify(base + i * ramp->stride);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_lanes);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
            Value *bitcastI = builder->CreateBitOrPointerCast(ptr, load_return_pointer_type);
            LoadInst *loadI = cast<LoadInst>(builder->CreateLoad(bitcastI));
#if LLVM_VERSION >= 110
            loadI->setAlignment(Align(alignment));
#else
            loadI->setAlignment(MaybeAlign(alignment));
#endif
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
                << (op->type.is_float() ? "f" : "i")
                << op->type.bits()
                << "x" << op->type.lanes();

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
            Type narrow = UInt(a.type().bits() / 2, a.type().lanes());
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
    } else if (op->is_intrinsic(Call::sorted_avg)) {
        Type ty = op->type;
        Type wide_ty = ty.widen();
        // This will codegen to vhaddu (arm32) or uhadd (arm64).
        value = codegen(cast(ty, (cast(wide_ty, op->args[0]) + cast(wide_ty, op->args[1])) / 2));
        return;
    } else if (op->is_intrinsic(Call::widening_mul) &&
               (op->type.is_int() || op->type.is_uint()) && op->type.lanes() > 1 &&
               !neon_intrinsics_disabled()) {
        std::string intrin;
        if (target.bits == 32) {
            intrin = "llvm.arm.neon.";
            if (op->type.is_uint()) {
                intrin += "vmullu.";
            } else {
                intrin += "vmulls.";
            }
        } else {
            intrin = "llvm.aarch64.neon.";
            if (op->type.is_uint()) {
                intrin += "umull.";
            } else {
                intrin += "smull.";
            }
        }
        int simd_bits = 128;
        intrin += "v" + std::to_string(simd_bits / op->type.bits()) + "i" + std::to_string(op->type.bits());
        value = call_intrin(op->type, simd_bits / op->type.bits(), intrin, {op->args[0], op->args[1]});
        return;
    } else if ((op->is_intrinsic(Call::halving_add) || op->is_intrinsic(Call::rounding_halving_add)) &&
               (op->type.is_int() || op->type.is_uint()) && op->type.lanes() > 1 &&
               !neon_intrinsics_disabled()) {
        std::string intrin;
        std::string rounding = op->is_intrinsic(Call::rounding_halving_add) ? "r" : "";
        if (target.bits == 32) {
            intrin = "llvm.arm.neon.";
            if (op->type.is_uint()) {
                intrin += "v" + rounding + "haddu.";
            } else {
                intrin += "v" + rounding + "hadds.";
            }
        } else {
            intrin = "llvm.aarch64.neon.";
            if (op->type.is_uint()) {
                intrin += "u" + rounding + "hadd.";
            } else {
                intrin += "s" + rounding + "hadd.";
            }
        }
        int simd_bits = op->type.lanes() * op->type.bits() > 64 ? 128 : 64;
        intrin += "v" + std::to_string(simd_bits / op->type.bits()) + "i" + std::to_string(op->type.bits());
        value = call_intrin(op->type, simd_bits / op->type.bits(), intrin, {op->args[0], op->args[1]});
        return;
    } else if (op->is_intrinsic(Call::halving_sub) &&
               (op->type.is_int() || op->type.is_uint()) && op->type.lanes() > 1 &&
               !neon_intrinsics_disabled()) {
        std::string intrin;
        std::string rounding = op->is_intrinsic(Call::rounding_halving_add) ? "r" : "";
        if (target.bits == 32) {
            intrin = "llvm.arm.neon.";
            if (op->type.is_uint()) {
                intrin += "vhsubu.";
            } else {
                intrin += "vhsubs.";
            }
        } else {
            intrin = "llvm.aarch64.neon.";
            if (op->type.is_uint()) {
                intrin += "uhsub.";
            } else {
                intrin += "shsub.";
            }
        }
        int simd_bits = op->type.lanes() * op->type.bits() > 64 ? 128 : 64;
        intrin += "v" + std::to_string(simd_bits / op->type.bits()) + "i" + std::to_string(op->type.bits());
        value = call_intrin(op->type, simd_bits / op->type.bits(), intrin, {op->args[0], op->args[1]});
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const LT *op) {
    if (op->a.type().is_float() && op->type.is_vector()) {
        // Fast-math flags confuse LLVM's aarch64 backend, so
        // temporarily clear them for this instruction.
        // See https://bugs.llvm.org/show_bug.cgi?id=45036
        llvm::IRBuilderBase::FastMathFlagGuard guard(*builder);
        builder->clearFastMathFlags();
        CodeGen_Posix::visit(op);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const LE *op) {
    if (op->a.type().is_float() && op->type.is_vector()) {
        // Fast-math flags confuse LLVM's aarch64 backend, so
        // temporarily clear them for this instruction.
        // See https://bugs.llvm.org/show_bug.cgi?id=45036
        llvm::IRBuilderBase::FastMathFlagGuard guard(*builder);
        builder->clearFastMathFlags();
        CodeGen_Posix::visit(op);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::codegen_vector_reduce(const VectorReduce *op, const Expr &init) {
    if (neon_intrinsics_disabled() ||
        op->op == VectorReduce::Or ||
        op->op == VectorReduce::And ||
        op->op == VectorReduce::Mul) {
        CodeGen_Posix::codegen_vector_reduce(op, init);
        return;
    }

    // ARM has a variety of pairwise reduction ops for +, min,
    // max. The versions that do not widen take two 64-bit args and
    // return one 64-bit vector of the same type. The versions that
    // widen take one arg and return something with half the vector
    // lanes and double the bit-width.

    int factor = op->value.type().lanes() / op->type.lanes();

    // These are the types for which we have reduce intrinsics in the
    // runtime.
    bool have_reduce_intrinsic = (op->type.is_int() ||
                                  op->type.is_uint() ||
                                  op->type.is_float());

    // We don't have 16-bit float or bfloat horizontal ops
    if (op->type.is_bfloat() || (op->type.is_float() && op->type.bits() < 32)) {
        have_reduce_intrinsic = false;
    }

    // Only aarch64 has float64 horizontal ops
    if (target.bits == 32 && op->type.element_of() == Float(64)) {
        have_reduce_intrinsic = false;
    }

    // For 64-bit integers, we only have addition, not min/max
    if (op->type.bits() == 64 &&
        !op->type.is_float() &&
        op->op != VectorReduce::Add) {
        have_reduce_intrinsic = false;
    }

    // We only have intrinsics that reduce by a factor of two
    if (factor != 2) {
        have_reduce_intrinsic = false;
    }

    if (have_reduce_intrinsic) {
        Expr arg = op->value;
        if (op->op == VectorReduce::Add &&
            op->type.bits() >= 16 &&
            !op->type.is_float()) {
            Type narrower_type = arg.type().narrow();
            Expr narrower = lossless_cast(narrower_type, arg);
            if (!narrower.defined() && arg.type().is_int()) {
                // We can also safely accumulate from a uint into a
                // wider int, because the addition uses at most one
                // extra bit.
                narrower = lossless_cast(narrower_type.with_code(Type::UInt), arg);
            }
            if (narrower.defined()) {
                arg = narrower;
            }
        }
        int output_bits;
        if (target.bits == 32 && arg.type().bits() == op->type.bits()) {
            // For the non-widening version, the output must be 64-bit
            output_bits = 64;
        } else if (op->type.bits() * op->type.lanes() <= 64) {
            // No point using the 128-bit version of the instruction if the output is narrow.
            output_bits = 64;
        } else {
            output_bits = 128;
        }

        const int output_lanes = output_bits / op->type.bits();
        Type intrin_type = op->type.with_lanes(output_lanes);
        Type arg_type = arg.type().with_lanes(output_lanes * 2);
        if (op->op == VectorReduce::Add &&
            arg.type().bits() == op->type.bits() &&
            arg_type.is_uint()) {
            // For non-widening additions, there is only a signed
            // version (because it's equivalent).
            arg_type = arg_type.with_code(Type::Int);
            intrin_type = intrin_type.with_code(Type::Int);
        } else if (arg.type().is_uint() && intrin_type.is_int()) {
            // Use the uint version
            intrin_type = intrin_type.with_code(Type::UInt);
        }

        std::stringstream ss;
        vector<Expr> args;
        ss << "pairwise_" << op->op << "_" << intrin_type << "_" << arg_type;
        Expr accumulator = init;
        if (op->op == VectorReduce::Add &&
            accumulator.defined() &&
            arg_type.bits() < intrin_type.bits()) {
            // We can use the accumulating variant
            ss << "_accumulate";
            args.push_back(init);
            accumulator = Expr();
        }
        args.push_back(arg);
        value = call_intrin(op->type, output_lanes, ss.str(), args);

        if (accumulator.defined()) {
            // We still have an initial value to take care of
            string n = unique_name('t');
            sym_push(n, value);
            Expr v = Variable::make(accumulator.type(), n);
            switch (op->op) {
            case VectorReduce::Add:
                accumulator += v;
                break;
            case VectorReduce::Min:
                accumulator = min(accumulator, v);
                break;
            case VectorReduce::Max:
                accumulator = max(accumulator, v);
                break;
            default:
                internal_error << "unreachable";
            }
            codegen(accumulator);
            sym_pop(n);
        }

        return;
    }

    // Pattern-match 8-bit dot product instructions available on newer
    // ARM cores.
    if (target.has_feature(Target::ARMDotProd) &&
        factor % 4 == 0 &&
        op->op == VectorReduce::Add &&
        target.bits == 64 &&
        (op->type.element_of() == Int(32) ||
         op->type.element_of() == UInt(32))) {
        const Mul *mul = op->value.as<Mul>();
        if (mul) {
            const int input_lanes = mul->type.lanes();
            Expr a = lossless_cast(UInt(8, input_lanes), mul->a);
            Expr b = lossless_cast(UInt(8, input_lanes), mul->b);
            if (!a.defined()) {
                a = lossless_cast(Int(8, input_lanes), mul->a);
                b = lossless_cast(Int(8, input_lanes), mul->b);
            }
            if (a.defined() && b.defined()) {
                if (factor != 4) {
                    Expr equiv = VectorReduce::make(op->op, op->value, input_lanes / 4);
                    equiv = VectorReduce::make(op->op, equiv, op->type.lanes());
                    codegen_vector_reduce(equiv.as<VectorReduce>(), init);
                    return;
                }
                Expr i = init;
                if (!i.defined()) {
                    i = make_zero(op->type);
                }
                vector<Expr> args{i, a, b};
                if (op->type.lanes() <= 2) {
                    if (op->type.is_uint()) {
                        value = call_intrin(op->type, 2, "llvm.aarch64.neon.udot.v2i32.v8i8", args);
                    } else {
                        value = call_intrin(op->type, 2, "llvm.aarch64.neon.sdot.v2i32.v8i8", args);
                    }
                } else {
                    if (op->type.is_uint()) {
                        value = call_intrin(op->type, 4, "llvm.aarch64.neon.udot.v4i32.v16i8", args);
                    } else {
                        value = call_intrin(op->type, 4, "llvm.aarch64.neon.sdot.v4i32.v16i8", args);
                    }
                }
                return;
            }
        }
    }

    CodeGen_Posix::codegen_vector_reduce(op, init);
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
        } else if (target.os == Target::OSX) {
            return "apple-a12";
        } else {
            return "generic";
        }
    }
}

string CodeGen_ARM::mattrs() const {
    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            return "+neon";
        }
        if (!target.has_feature(Target::NoNEON)) {
            return "+neon";
        } else {
            return "-neon";
        }
    } else {
        // TODO: Should Halide's SVE flags be 64-bit only?
        string arch_flags;
        string separator;
        if (target.has_feature(Target::SVE2)) {
            arch_flags = "+sve2";
            separator = ",";
        } else if (target.has_feature(Target::SVE)) {
            arch_flags = "+sve";
            separator = ",";
        }

        if (target.has_feature(Target::ARMDotProd)) {
            arch_flags += separator + "+dotprod";
            separator = ",";
        }

        if (target.os == Target::IOS || target.os == Target::OSX) {
            return arch_flags + separator + "+reserve-x18";
        } else {
            return arch_flags;
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

}  // namespace Internal
}  // namespace Halide

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

// Native client llvm relies on global flags to control sandboxing on
// arm, because they expect you to be coming from the command line.
#ifdef WITH_NATIVE_CLIENT
#if LLVM_VERSION < 34
#include <llvm/Support/CommandLine.h>
namespace llvm {
extern cl::opt<bool> FlagSfiData,
    FlagSfiLoad,
    FlagSfiStore,
    FlagSfiStack,
    FlagSfiBranch,
    FlagSfiDisableCP,
    FlagSfiZeroMask;
}
extern llvm::cl::opt<bool> ReserveR9;
#endif
#endif

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
    return cast(Int(64, e.type().width), e);
}

Expr _u64(Expr e) {
    return cast(UInt(64, e.type().width), e);
}
Expr _i32(Expr e) {
    return cast(Int(32, e.type().width), e);
}

Expr _u32(Expr e) {
    return cast(UInt(32, e.type().width), e);
}

Expr _i16(Expr e) {
    return cast(Int(16, e.type().width), e);
}

Expr _u16(Expr e) {
    return cast(UInt(16, e.type().width), e);
}

#if LLVM_VERSION < 35
Expr _i8(Expr e) {
    return cast(Int(8, e.type().width), e);
}

Expr _u8(Expr e) {
    return cast(UInt(8, e.type().width), e);
}
#endif

/*
Expr _f32(Expr e) {
    return cast(Float(32, e.type().width), e);
}

Expr _f64(Expr e) {
    return cast(Float(64, e.type().width), e);
}
*/

// saturating cast operators
Expr _i8q(Expr e) {
    return cast(Int(8, e.type().width), clamp(e, -128, 127));
}

Expr _u8q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(8, e.type().width), min(e, 255));
    } else {
        return cast(UInt(8, e.type().width), clamp(e, 0, 255));
    }
}

Expr _i16q(Expr e) {
    return cast(Int(16, e.type().width), clamp(e, -32768, 32767));
}

Expr _u16q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(16, e.type().width), min(e, 65535));
    } else {
        return cast(UInt(16, e.type().width), clamp(e, 0, 65535));
    }
}

Expr _i32q(Expr e) {
    return cast(Int(32, e.type().width), clamp(e, Int(32).min(), Int(32).max()));
}

Expr _u32q(Expr e) {
    if (e.type().is_uint()) {
        return cast(UInt(32, e.type().width), min(e, UInt(32).max()));
    } else {
        return cast(UInt(32, e.type().width), clamp(e, 0, UInt(32).max()));
    }
}
}

CodeGen_ARM::CodeGen_ARM(Target t) : CodeGen_Posix(t) {
    if (t.bits == 32) {
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
    user_assert(t.os != Target::NaCl) << "llvm build not configured with native client enabled\n.";
    #endif

    // These patterns went away in llvm commit r189481, which is
    // unfortunate, because they don't get generated automatically in
    // the signed case.
    #if LLVM_VERSION < 34
    if (target.bits == 32) {
        casts.push_back(Pattern("vaddhn.v8i8", 8, _i8((wild_i16x8 + wild_i16x8)/256)));
        casts.push_back(Pattern("vaddhn.v4i16", 4, _i16((wild_i32x4 + wild_i32x4)/65536)));
        casts.push_back(Pattern("vaddhn.v8i8", 8, _u8((wild_u16x8 + wild_u16x8)/256)));
        casts.push_back(Pattern("vaddhn.v4i16", 4, _u16((wild_u32x4 + wild_u32x4)/65536)));
        casts.push_back(Pattern("vsubhn.v8i8", 8, _i8((wild_i16x8 - wild_i16x8)/256)));
        casts.push_back(Pattern("vsubhn.v4i16", 4, _i16((wild_i32x4 - wild_i32x4)/65536)));
        casts.push_back(Pattern("vsubhn.v8i8", 8, _u8((wild_u16x8 - wild_u16x8)/256)));
        casts.push_back(Pattern("vsubhn.v4i16", 4, _u16((wild_u32x4 - wild_u32x4)/65536)));
    }
    #endif

    if (target.bits == 32) {
        // Generate the cast patterns that can take vector types.  We need
        // to iterate over all 64 and 128 bit integer types relevant for
        // neon.
        Type types[] = {Int(8, 8), Int(8, 16), UInt(8, 8), UInt(8, 16),
                        Int(16, 4), Int(16, 8), UInt(16, 4), UInt(16, 8),
                        Int(32, 2), Int(32, 4), UInt(32, 2), UInt(32, 4)};
        for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
            Type t = types[i];

            int intrin_width = t.width;
            std::ostringstream oss;
            oss << (t.is_int() ? 's' : 'u') << ".v" << intrin_width << "i" << t.bits;
            string t_str = oss.str();

            // For the 128-bit versions, we want to match any vector width.
            if (t.bits * t.width == 128) {
                t.width = -1;
            }

            // Wider versions of the type
            Type w = t;
            w.bits *= 2;
            Type ws = Int(t.bits*2, t.width);

            // Vector wildcard for this type
            Expr vector = Variable::make(t, "*");
            Expr w_vector = Variable::make(w, "*");
            Expr ws_vector = Variable::make(ws, "*");

            // Bounds of the type stored in the wider vector type
            Expr tmin = simplify(cast(w, t.imin()));
            Expr tmax = simplify(cast(w, t.imax()));
            Expr tsmin = simplify(cast(ws, t.imin()));
            Expr tsmax = simplify(cast(ws, t.imax()));

            // Can't fit uint32 max into an intimm
            if (t.element_of() == UInt(32)) {
                tmax = simplify(cast(w, t.max()));
                tsmax = simplify(cast(ws, t.max()));
            }

            Pattern p("", intrin_width, Expr(), Pattern::NarrowArgs);

            // Rounding-up averaging
            p.intrin = "llvm.arm.neon.vrhadd" + t_str;
            p.pattern = cast(t, (w_vector + w_vector + 1)/2);
            casts.push_back(p);
            p.pattern = cast(t, (w_vector + (w_vector + 1))/2);
            casts.push_back(p);
            p.pattern = cast(t, ((w_vector + 1) + w_vector)/2);
            casts.push_back(p);

            // Rounding down averaging
            p.intrin = "llvm.arm.neon.vhadd" + t_str;
            p.pattern = cast(t, (w_vector + w_vector)/2);
            casts.push_back(p);

            // Halving subtract
            p.intrin = "llvm.arm.neon.vhsub" + t_str;
            p.pattern = cast(t, (w_vector - w_vector)/2);
            casts.push_back(p);

            // Saturating add
            p.intrin = "llvm.arm.neon.vqadd" + t_str;
            p.pattern = cast(t, clamp(w_vector + w_vector, tmin, tmax));
            casts.push_back(p);

            // In the unsigned case, the saturation below is unnecessary
            if (t.is_uint()) {
                p.pattern = cast(t, min(w_vector + w_vector, tmax));
                casts.push_back(p);
            }

            // Saturating subtract
            // N.B. Saturating subtracts always widen to a signed type
            p.intrin = "llvm.arm.neon.vqsub" + t_str;
            p.pattern = cast(t, clamp(ws_vector - ws_vector, tsmin, tsmax));
            casts.push_back(p);

            // In the unsigned case, we may detect that the top of the clamp is unnecessary
            if (t.is_uint()) {
                p.pattern = cast(t, max(ws_vector - ws_vector, 0));
                casts.push_back(p);
            }
        }

        // At some point llvm started recognising narrowing shifts
        // directly and these intrinsics went away.
#if LLVM_VERSION < 35
        // Narrowing right shifts
        casts.push_back(Pattern("vshiftn.v8i8",  8,  _i8(wild_i16x_/wild_i16x_), Pattern::RightShift));
        casts.push_back(Pattern("vshiftn.v4i16", 4, _i16(wild_i32x_/wild_i32x_), Pattern::RightShift));
        casts.push_back(Pattern("vshiftn.v2i32", 2, _i32(wild_i64x_/wild_i64x_), Pattern::RightShift));
        casts.push_back(Pattern("vshiftn.v8i8",  8,  _u8(wild_u16x_/wild_u16x_), Pattern::RightShift));
        casts.push_back(Pattern("vshiftn.v4i16", 4, _u16(wild_u32x_/wild_u32x_), Pattern::RightShift));
        casts.push_back(Pattern("vshiftn.v2i32", 2, _u32(wild_u64x_/wild_u64x_), Pattern::RightShift));

        // Widening left shifts
        left_shifts.push_back(Pattern("vshiftls.v8i16", 8, _i16(wild_i8x_)*wild_i16x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftls.v4i32", 4, _i32(wild_i16x_)*wild_i32x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftls.v2i64", 2, _i64(wild_i32x_)*wild_i64x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftlu.v8i16", 8, _u16(wild_u8x_)*wild_u16x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftlu.v4i32", 4, _u32(wild_u16x_)*wild_u32x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftlu.v2i64", 2, _u64(wild_u32x_)*wild_u64x_, Pattern::LeftShift));
#endif

        casts.push_back(Pattern("vqshiftns.v8i8",  8, _i8q(wild_i16x_/wild_i16x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftns.v4i16", 4, _i16q(wild_i32x_/wild_i32x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftns.v2i32", 2, _i32q(wild_i64x_/wild_i64x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnu.v8i8",  8, _u8q(wild_u16x_/wild_u16x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnu.v4i16", 4, _u16q(wild_u32x_/wild_u32x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnu.v2i32", 2, _u32q(wild_u64x_/wild_u64x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnsu.v8i8",  8, _u8q(wild_i16x_/wild_i16x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnsu.v4i16", 4, _u16q(wild_i32x_/wild_i32x_), Pattern::RightShift));
        casts.push_back(Pattern("vqshiftnsu.v2i32", 2, _u32q(wild_i64x_/wild_i64x_), Pattern::RightShift));

        // Where a 64-bit and 128-bit version exist, we use the 64-bit
        // version only when the args are 64-bits wide.
        casts.push_back(Pattern("vqshifts.v8i8",   8, _i8q(_i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
        casts.push_back(Pattern("vqshifts.v4i16",  4, _i16q(_i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
        casts.push_back(Pattern("vqshifts.v2i32",  2, _i32q(_i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v8i8",   8, _u8q(_u16(wild_u8x8)*wild_u16x8), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v4i16",  4, _u16q(_u32(wild_u16x4)*wild_u32x4), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v2i32",  2, _u32q(_u64(wild_u32x2)*wild_u64x2), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v8i8",  8, _u8q(_i16(wild_i8x8)*wild_i16x8), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v4i16", 4, _u16q(_i32(wild_i16x4)*wild_i32x4), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v2i32", 2, _u32q(_i64(wild_i32x2)*wild_i64x2), Pattern::LeftShift));

        // We use the 128-bit version for all other vector widths.
        casts.push_back(Pattern("vqshifts.v16i8", 16, _i8q(_i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshifts.v8i16",  8, _i16q(_i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshifts.v4i32",  4, _i32q(_i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v16i8", 16, _u8q(_u16(wild_u8x_)*wild_u16x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v8i16",  8, _u16q(_u32(wild_u16x_)*wild_u32x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftu.v4i32",  4, _u32q(_u64(wild_u32x_)*wild_u64x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v16i8", 16, _u8q(_i16(wild_i8x_)*wild_i16x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v8i16", 8, _u16q(_i32(wild_i16x_)*wild_i32x_), Pattern::LeftShift));
        casts.push_back(Pattern("vqshiftsu.v4i32", 4, _u32q(_i64(wild_i32x_)*wild_i64x_), Pattern::LeftShift));

        casts.push_back(Pattern("vqmovns.v8i8",   8,  _i8q(wild_i16x_)));
        casts.push_back(Pattern("vqmovns.v4i16",  4, _i16q(wild_i32x_)));
        casts.push_back(Pattern("vqmovns.v2i32",  2, _i32q(wild_i64x_)));
        casts.push_back(Pattern("vqmovnu.v8i8",   8,  _u8q(wild_u16x_)));
        casts.push_back(Pattern("vqmovnu.v4i16",  4, _u16q(wild_u32x_)));
        casts.push_back(Pattern("vqmovnu.v2i32",  2, _u32q(wild_u64x_)));
        casts.push_back(Pattern("vqmovnsu.v8i8",  8,  _u8q(wild_i16x_)));
        casts.push_back(Pattern("vqmovnsu.v4i16", 4, _u16q(wild_i32x_)));
        casts.push_back(Pattern("vqmovnsu.v2i32", 2, _u32q(wild_i64x_)));

        // Non-widening left shifts

        // 64-bit
        left_shifts.push_back(Pattern("vshifts.v8i8",  8, wild_i8x8*wild_i8x8, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshifts.v4i16", 4, wild_i16x4*wild_i16x4, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshifts.v2i32", 2, wild_i32x2*wild_i32x2, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v8i8",  8, wild_u8x8*wild_u8x8, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v4i16", 4, wild_u16x4*wild_u16x4, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v2i32", 2, wild_u32x2*wild_u32x2, Pattern::LeftShift));

        // 128-bit
        left_shifts.push_back(Pattern("vshifts.v16i8", 16,  wild_i8x_*wild_i8x_,  Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshifts.v8i16",  8, wild_i16x_*wild_i16x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshifts.v4i32",  4, wild_i32x_*wild_i32x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshifts.v2i64",  2, wild_i64x_*wild_i64x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v16i8", 16,  wild_u8x_*wild_u8x_,  Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v8i16",  8, wild_u16x_*wild_u16x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v4i32",  4, wild_u32x_*wild_u32x_, Pattern::LeftShift));
        left_shifts.push_back(Pattern("vshiftu.v2i64",  2, wild_u64x_*wild_u64x_, Pattern::LeftShift));

        // 64-bit averaging round-down
        averagings.push_back(Pattern("vhadds.v8i8",  8, (wild_i8x8 + wild_i8x8)));
        averagings.push_back(Pattern("vhaddu.v8i8",  8, (wild_u8x8 + wild_u8x8)));
        averagings.push_back(Pattern("vhadds.v4i16", 4, (wild_i16x4 + wild_i16x4)));
        averagings.push_back(Pattern("vhaddu.v4i16", 4, (wild_u16x4 + wild_u16x4)));
        averagings.push_back(Pattern("vhadds.v2i32", 2, (wild_i32x2 + wild_i32x2)));
        averagings.push_back(Pattern("vhaddu.v2i32", 2, (wild_u32x2 + wild_u32x2)));

        // 128-bit
        averagings.push_back(Pattern("vhadds.v16i8", 16, (wild_i8x_  + wild_i8x_)));
        averagings.push_back(Pattern("vhaddu.v16i8", 16, (wild_u8x_  + wild_u8x_)));
        averagings.push_back(Pattern("vhadds.v8i16",  8, (wild_i16x_ + wild_i16x_)));
        averagings.push_back(Pattern("vhaddu.v8i16",  8, (wild_u16x_ + wild_u16x_)));
        averagings.push_back(Pattern("vhadds.v4i32",  4, (wild_i32x_ + wild_i32x_)));
        averagings.push_back(Pattern("vhaddu.v4i32",  4, (wild_u32x_ + wild_u32x_)));

        // 64-bit halving subtract
        averagings.push_back(Pattern("vhsubs.v8i8",  8, (wild_i8x8  - wild_i8x8)));
        averagings.push_back(Pattern("vhsubu.v8i8",  8, (wild_u8x8  - wild_u8x8)));
        averagings.push_back(Pattern("vhsubs.v4i16", 4, (wild_i16x4 - wild_i16x4)));
        averagings.push_back(Pattern("vhsubu.v4i16", 4, (wild_u16x4 - wild_u16x4)));
        averagings.push_back(Pattern("vhsubs.v2i32", 2, (wild_i32x2 - wild_i32x2)));
        averagings.push_back(Pattern("vhsubu.v2i32", 2, (wild_u32x2 - wild_u32x2)));

        // 128-bit
        averagings.push_back(Pattern("vhsubs.v16i8", 16, (wild_i8x_  - wild_i8x_)));
        averagings.push_back(Pattern("vhsubu.v16i8", 16, (wild_u8x_  - wild_u8x_)));
        averagings.push_back(Pattern("vhsubs.v8i16",  8, (wild_i16x_ - wild_i16x_)));
        averagings.push_back(Pattern("vhsubu.v8i16",  8, (wild_u16x_ - wild_u16x_)));
        averagings.push_back(Pattern("vhsubs.v4i32",  4, (wild_i32x_ - wild_i32x_)));
        averagings.push_back(Pattern("vhsubu.v4i32",  4, (wild_u32x_ - wild_u32x_)));

        // 64-bit saturating negation
        negations.push_back(Pattern("vqneg.v8i8",  8, -max(wild_i8x8, -127)));
        negations.push_back(Pattern("vqneg.v4i16", 4, -max(wild_i16x4, -32767)));
        negations.push_back(Pattern("vqneg.v2i32", 2, -max(wild_i32x2, -(0x7fffffff))));

        // 128-bit
        negations.push_back(Pattern("vqneg.v16i8", 16, -max(wild_i8x_, -127)));
        negations.push_back(Pattern("vqneg.v8i16", 8,  -max(wild_i16x_, -32767)));
        negations.push_back(Pattern("vqneg.v4i32", 4,  -max(wild_i32x_, -(0x7fffffff))));
    }
}

llvm::Triple CodeGen_ARM::get_target_triple() const {
    llvm::Triple triple;

    if (target.bits == 32) {
        if (target.has_feature(Target::ARMv7s)) {
            triple.setArchName("armv7s");
        } else {
            triple.setArch(llvm::Triple::arm);
        }
    } else {
        user_assert(target.bits == 64) << "Target bits must be 32 or 64\n";
        #if (WITH_AARCH64)
        triple.setArch(llvm::Triple::aarch64);
        #else
        user_error << "AArch64 llvm target not enabled in this build of Halide\n";
        #endif
    }

    if (target.os == Target::Android) {
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::EABI);
    } else if (target.os == Target::IOS) {
        triple.setOS(llvm::Triple::IOS);
        triple.setVendor(llvm::Triple::Apple);
    } else if (target.os == Target::NaCl) {
        user_assert(target.bits == 32) << "ARM NaCl must be 32-bit\n";
        #ifdef WITH_NATIVE_CLIENT
        triple.setOS(llvm::Triple::NaCl);
        triple.setEnvironment(llvm::Triple::EABI);
        // The ARM Nacl backend relies on global switches being set to do
        // the sandboxing, so set them here.
        #if LLVM_VERSION < 34
        llvm::FlagSfiData = true;
        llvm::FlagSfiLoad = true;
        llvm::FlagSfiStore = true;
        llvm::FlagSfiStack = true;
        llvm::FlagSfiBranch = true;
        llvm::FlagSfiDisableCP = true;
        llvm::FlagSfiZeroMask = false;
        ReserveR9 = true;
        #endif
        #else
        user_error << "This version of Halide was compiled without nacl support\b";
        #endif
    } else if (target.os == Target::Linux) {
        triple.setOS(llvm::Triple::Linux);
        triple.setEnvironment(llvm::Triple::GNUEABIHF);
    } else {
        user_error << "No arm support for this OS\n";
    }

    return triple;
}

namespace {

// Try to losslessly narrow an integer expression to the target type
Expr try_narrow(Expr a, Type target) {
    internal_assert(a.type().width == target.width);
    if (const Cast *c = a.as<Cast>()) {
        Type old_type = c->value.type();
        if (old_type == target) {
            return c->value;
        } else if (target.can_represent(old_type)) {
            return cast(target, c->value);
        } else if (c->type.can_represent(target)) {
            // We lose nothing by stripping off the cast and pressing onwards.
            return try_narrow(c->value, target);
        } else {
            return Expr();
        }
    }

    if (const Broadcast *b = a.as<Broadcast>()) {
        Expr n = try_narrow(b->value, target.element_of());
        if (n.defined()) {
            return Broadcast::make(n, b->width);
        } else {
            return Expr();
        }
    }

    if (const IntImm *i = a.as<IntImm>()) {
        if (i->value <= target.imax() &&
            i->value >= target.imin()) {
            return cast(target, a);
        }
    }

    return Expr();
}
}

void CodeGen_ARM::visit(const Cast *op) {
    // AArch64 SIMD not yet supported
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
                value = call_intrin(t, pattern.intrin_width, pattern.intrin, matches);
                return;
            } else if (pattern.type == Pattern::NarrowArgs) {
                // Try to narrow all of the args.
                bool all_narrow = true;
                for (size_t i = 0; i < matches.size(); i++) {
                    internal_assert(matches[i].type().bits == t.bits * 2);
                    internal_assert(matches[i].type().width == t.width);
                    // debug(4) << "Attemping to narrow " << matches[i] << " to " << t << "\n";
                    matches[i] = try_narrow(matches[i], t);
                    if (!matches[i].defined()) {
                        // debug(4) << "failed\n";
                        all_narrow = false;
                    } else {
                        // debug(4) << "success: " << matches[i] << "\n";
                        internal_assert(matches[i].type() == t);
                    }
                }

                if (all_narrow) {
                    value = call_intrin(t, pattern.intrin_width, pattern.intrin, matches);
                    return;
                }
            } else { // must be a shift
                Expr constant = matches[1];
                int shift_amount;
                bool power_of_two = is_const_power_of_two(constant, &shift_amount);
                if (power_of_two && shift_amount < matches[0].type().bits) {
                    if (pattern.type == Pattern::RightShift) {
                        shift_amount = -shift_amount;
                    } else {
                        internal_assert(pattern.type == Pattern::LeftShift);
                    }
                    Value *shift = ConstantInt::get(llvm_type_of(matches[0].type()),
                                                    shift_amount);
                    value = call_intrin(llvm_type_of(t),
                                        pattern.intrin_width,
                                        pattern.intrin,
                                        vec(codegen(matches[0]), shift));
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
    if ((t.is_int() || t.is_uint()) &&
        op->value.type().is_int() &&
        t.bits == op->value.type().bits / 2) {
        const Div *d = op->value.as<Div>();
        if (d && is_const(d->b, 1 << t.bits)) {
            Type unsigned_type = UInt(t.bits * 2, t.width);
            Expr replacement = cast(t,
                                    cast(unsigned_type, d->a) /
                                    cast(unsigned_type, d->b));
            replacement.accept(this);
            return;
        }
    }

    // Catch widening of absolute difference
    if ((t.is_int() || t.is_uint()) &&
        (op->value.type().is_int() || op->value.type().is_uint()) &&
        t.bits == op->value.type().bits * 2) {
        Expr a, b;
        const Call *c = op->value.as<Call>();
        if (c && c->name == Call::absd && c->call_type == Call::Intrinsic) {
            ostringstream ss;
            int intrin_width = 128 / t.bits;
            ss << "vabdl_" << (c->args[0].type().is_int() ? 'i' : 'u') << t.bits / 2 << 'x' << intrin_width;
            value = call_intrin(t, intrin_width, ss.str(), c->args);
            return;
        }
    }

    CodeGen_Posix::visit(op);

}

void CodeGen_ARM::visit(const Mul *op) {
    // AArch64 SIMD not yet supported
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
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);
    if (power_of_two) {
        for (size_t i = 0; i < left_shifts.size(); i++) {
            const Pattern &pattern = left_shifts[i];
            internal_assert(pattern.type == Pattern::LeftShift);
            if (expr_match(pattern.pattern, op, matches)) {
                llvm::Type *t_arg = llvm_type_of(matches[0].type());
                llvm::Type *t_result = llvm_type_of(op->type);
                Value *shift = ConstantInt::get(t_arg, shift_amount);
                value = call_intrin(t_result,
                                    pattern.intrin_width, pattern.intrin,
                                    vec(codegen(matches[0]), shift));
                return;
            }
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Div *op) {
    // AArch64 SIMD not yet supported
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    if (op->type.is_vector() && is_two(op->b) &&
        (op->a.as<Add>() || op->a.as<Sub>())) {
        vector<Expr> matches;
        for (size_t i = 0; i < averagings.size(); i++) {
            if (expr_match(averagings[i].pattern, op->a, matches)) {
                value = call_intrin(op->type, averagings[i].intrin_width,
                                    averagings[i].intrin, matches);
                return;
            }
        }
    }

    // Detect if it's a small int division
    const Broadcast *broadcast = op->b.as<Broadcast>();
    const Cast *cast_b = broadcast ? broadcast->value.as<Cast>() : NULL;
    const IntImm *int_imm = cast_b ? cast_b->value.as<IntImm>() : NULL;
    if (broadcast && !int_imm) int_imm = broadcast->value.as<IntImm>();
    if (!int_imm) int_imm = op->b.as<IntImm>();
    int const_divisor = int_imm ? int_imm->value : 0;

    // Check if the divisor is a power of two
    int shift_amount;
    bool power_of_two = is_const_power_of_two(op->b, &shift_amount);

    vector<Expr> matches;
    if (power_of_two && op->type.is_int()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateAShr(numerator, shift);
    } else if (power_of_two && op->type.is_uint()) {
        Value *numerator = codegen(op->a);
        Constant *shift = ConstantInt::get(llvm_type_of(op->type), shift_amount);
        value = builder->CreateLShr(numerator, shift);
    } else if (op->type.is_int() &&
               (op->type.bits == 32 || op->type.bits == 16 || op->type.bits == 8) &&
               const_divisor > 1 &&
               ((op->type.bits > 8 && const_divisor < 256) || const_divisor < 128)) {

        int64_t multiplier, shift;
        if (op->type.bits == 32) {
            multiplier = IntegerDivision::table_s32[const_divisor][2];
            shift      = IntegerDivision::table_s32[const_divisor][3];
        } else if (op->type.bits == 16) {
            multiplier = IntegerDivision::table_s16[const_divisor][2];
            shift      = IntegerDivision::table_s16[const_divisor][3];
        } else {
            // 8 bit
            multiplier = IntegerDivision::table_s8[const_divisor][2];
            shift      = IntegerDivision::table_s8[const_divisor][3];
        }

        Value *val = codegen(op->a);

        // Make an all-ones mask if the numerator is negative
        Value *sign = builder->CreateAShr(val, codegen(make_const(op->type, op->type.bits-1)));
        // Flip the numerator bits if the mask is high
        Value *flipped = builder->CreateXor(sign, val);
        // Grab the multiplier
        Value *mult = codegen(make_const(op->type, (int)multiplier));
        // Widening multiply
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(Int(op->type.bits*2, op->type.width));
        // flipped's high bit is zero, so it's ok to zero-extend it
        Value *flipped_wide = builder->CreateIntCast(flipped, wider, false);
        Value *mult_wide = builder->CreateIntCast(mult, wider, false);
        Value *wide_val = builder->CreateMul(flipped_wide, mult_wide);
        // Do the shift (add 8 or 16 to narrow back down)
        #if LLVM_VERSION < 35
        if (op->type.bits == 32 && op->type.is_vector() && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -32);
            val = call_intrin(narrower, 2, "llvm.arm.neon.vshiftn.v2i32", vec<Value *>(wide_val, shift_amount));
        } else if (op->type.bits == 16 && op->type.is_vector() && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -16);
            val = call_intrin(narrower, 4, "llvm.arm.neon.vshiftn.v4i16", vec<Value *>(wide_val, shift_amount));
        } else if (op->type.bits == 8 && op->type.is_vector() && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -8);
            val = call_intrin(narrower, 8, "llvm.arm.neon.vshiftn.v8i8", vec<Value *>(wide_val, shift_amount));
        } else
        #endif
        {
            Constant *shift_amount = ConstantInt::get(wider, (shift + op->type.bits));
            val = builder->CreateLShr(wide_val, shift_amount);
            val = builder->CreateIntCast(val, narrower, true);
        }
        // Maybe flip the bits again
        value = builder->CreateXor(val, sign);

    } else if (op->type.is_uint() &&
               (op->type.bits == 32 || op->type.bits == 16 || op->type.bits == 8) &&
               const_divisor > 1 && const_divisor < 256) {

        int64_t method, multiplier, shift;
        if (op->type.bits == 32) {
            method     = IntegerDivision::table_u32[const_divisor][1];
            multiplier = IntegerDivision::table_u32[const_divisor][2];
            shift      = IntegerDivision::table_u32[const_divisor][3];
        } else if (op->type.bits == 16) {
            method     = IntegerDivision::table_u16[const_divisor][1];
            multiplier = IntegerDivision::table_u16[const_divisor][2];
            shift      = IntegerDivision::table_u16[const_divisor][3];
        } else {
            method     = IntegerDivision::table_u8[const_divisor][1];
            multiplier = IntegerDivision::table_u8[const_divisor][2];
            shift      = IntegerDivision::table_u8[const_divisor][3];
        }

        internal_assert(method != 0)
            << "method 0 division is for powers of two and should have been handled elsewhere\n";

        Value *num = codegen(op->a);

        // Widen
        llvm::Type *narrower = llvm_type_of(op->type);
        llvm::Type *wider = llvm_type_of(UInt(op->type.bits*2, op->type.width));
        Value *mult = ConstantInt::get(narrower, multiplier);
        mult = builder->CreateIntCast(mult, wider, false);
        Value *val = builder->CreateIntCast(num, wider, false);

        // Multiply
        val = builder->CreateMul(val, mult);

        // Narrow
        #if LLVM_VERSION < 35
        if (op->type.bits == 32 && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -32);
            val = call_intrin(narrower, 2, "llvm.arm.neon.vshiftn.v2i32", vec<Value *>(val, shift_amount));
        } else if (op->type.bits == 16 && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -16);
            val = call_intrin(narrower, 4, "llvm.arm.neon.vshiftn.v4i16", vec<Value *>(val, shift_amount));
        } else if (op->type == UInt(8, 8) && shift == 0) {
            Constant *shift_amount = ConstantInt::get(wider, -8);
            val = call_intrin(narrower, 8, "llvm.arm.neon.vshiftn.v8i8", vec<Value *>(val, shift_amount));
        } else
        #endif
        {
            int shift_bits = op->type.bits;
            // For method 1, we can do the final shift here too.
            if (method == 1) {
                shift_bits += (int)shift;
            }
            Constant *shift_amount = ConstantInt::get(wider, shift_bits);
            val = builder->CreateLShr(val, shift_amount);
            val = builder->CreateIntCast(val, narrower, false);
        }

        // Average with original numerator
        if (method == 2) {
            if (op->type.bits == 32) {
                val = call_intrin(narrower, 2, "llvm.arm.neon.vhaddu.v2i32", vec(val, num));
            } else if (op->type.bits == 16) {
                val = call_intrin(narrower, 4, "llvm.arm.neon.vhaddu.v4i16", vec(val, num));
            } else if (op->type.bits == 8) {
                val = call_intrin(narrower, 8, "llvm.arm.neon.vhaddu.v8i8", vec(val, num));
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
    // AArch64 SIMD not yet supported
    if (neon_intrinsics_disabled()) {
        CodeGen_Posix::visit(op);
        return;
    }

    vector<Expr> matches;
    for (size_t i = 0; i < negations.size(); i++) {
        if (op->type.is_vector() &&
            expr_match(negations[i].pattern, op, matches)) {
            value = call_intrin(op->type, negations[i].intrin_width,
                                negations[i].intrin, matches);
            return;
        }
    }

    // llvm will generate floating point negate instructions if we ask for (-0.0f)-x
    if (op->type.is_float() && is_zero(op->a)) {
        Constant *a;
        if (op->type.bits == 32) {
            a = ConstantFP::getNegativeZero(f32);
        } else if (op->type.bits == 64) {
            a = ConstantFP::getNegativeZero(f64);
        } else {
            a = NULL;
            internal_error << "Unknown bit width for floating point type: " << op->type << "\n";
        }

        Value *b = codegen(op->b);

        if (op->type.width > 1) {
            a = ConstantVector::getSplat(op->type.width, a);
        }
        value = builder->CreateFSub(a, b);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Min *op) {
    // AArch64 SIMD not yet supported
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
        Value *wide_result = call_intrin(f32x2, 2, "llvm.arm.neon.vmins.v2f32", vec(a_wide, b_wide));
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "llvm.arm.neon.vminu.v8i8"},
        {UInt(16, 4), "llvm.arm.neon.vminu.v4i16"},
        {UInt(32, 2), "llvm.arm.neon.vminu.v2i32"},
        {Int(8, 8), "llvm.arm.neon.vmins.v8i8"},
        {Int(16, 4), "llvm.arm.neon.vmins.v4i16"},
        {Int(32, 2), "llvm.arm.neon.vmins.v2i32"},
        {Float(32, 2), "llvm.arm.neon.vmins.v2f32"},
        {UInt(8, 16), "llvm.arm.neon.vminu.v16i8"},
        {UInt(16, 8), "llvm.arm.neon.vminu.v8i16"},
        {UInt(32, 4), "llvm.arm.neon.vminu.v4i32"},
        {Int(8, 16), "llvm.arm.neon.vmins.v16i8"},
        {Int(16, 8), "llvm.arm.neon.vmins.v8i16"},
        {Int(32, 4), "llvm.arm.neon.vmins.v4i32"},
        {Float(32, 4), "llvm.arm.neon.vmins.v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        bool match = op->type == patterns[i].t;

        // The 128-bit versions are also used for other vector widths.
        if (op->type.is_vector() && patterns[i].t.width * patterns[i].t.bits == 128) {
            match = match || (op->type.element_of() == patterns[i].t.element_of());
        }

        if (match) {
            value = call_intrin(op->type, patterns[i].t.width, patterns[i].op, vec(op->a, op->b));
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    // AArch64 SIMD not yet supported
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
        Value *wide_result = call_intrin(f32x2, 2, "llvm.arm.neon.vmaxs.v2f32", vec(a_wide, b_wide));
        value = builder->CreateExtractElement(wide_result, zero);
        return;
    }

    struct {
        Type t;
        const char *op;
    } patterns[] = {
        {UInt(8, 8), "llvm.arm.neon.vmaxu.v8i8"},
        {UInt(16, 4), "llvm.arm.neon.vmaxu.v4i16"},
        {UInt(32, 2), "llvm.arm.neon.vmaxu.v2i32"},
        {Int(8, 8), "llvm.arm.neon.vmaxs.v8i8"},
        {Int(16, 4), "llvm.arm.neon.vmaxs.v4i16"},
        {Int(32, 2), "llvm.arm.neon.vmaxs.v2i32"},
        {Float(32, 2), "llvm.arm.neon.vmaxs.v2f32"},
        {UInt(8, 16), "llvm.arm.neon.vmaxu.v16i8"},
        {UInt(16, 8), "llvm.arm.neon.vmaxu.v8i16"},
        {UInt(32, 4), "llvm.arm.neon.vmaxu.v4i32"},
        {Int(8, 16), "llvm.arm.neon.vmaxs.v16i8"},
        {Int(16, 8), "llvm.arm.neon.vmaxs.v8i16"},
        {Int(32, 4), "llvm.arm.neon.vmaxs.v4i32"},
        {Float(32, 4), "llvm.arm.neon.vmaxs.v4f32"}
    };

    for (size_t i = 0; i < sizeof(patterns)/sizeof(patterns[0]); i++) {
        bool match = op->type == patterns[i].t;

        // The 128-bit versions are also used for other vector widths.
        if (op->type.is_vector() && patterns[i].t.width * patterns[i].t.bits == 128) {
            match = match || (op->type.element_of() == patterns[i].t.element_of());
        }

        if (match) {
            value = call_intrin(op->type, patterns[i].t.width, patterns[i].op, vec(op->a, op->b));
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Store *op) {
    // AArch64 SIMD not yet supported
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
    vector<pair<string, Expr> > lets;
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
        int vec_bits = t.bits * t.width;
        if (elt == Float(32) ||
            elt == Int(8) || elt == Int(16) || elt == Int(32) ||
            elt == UInt(8) || elt == UInt(16) || elt == UInt(32)) {
            if (vec_bits % 128 == 0) {
                type_ok_for_vst = true;
                intrin_type.width = 128 / t.bits;
            } else if (vec_bits % 64 == 0) {
                type_ok_for_vst = true;
                intrin_type.width = 64 / t.bits;
            }
        }
    }

    if (is_one(ramp->stride) &&
        call && call->call_type == Call::Intrinsic &&
        call->name == Call::interleave_vectors &&
        type_ok_for_vst &&
        2 <= call->args.size() && call->args.size() <= 4) {

        const int num_vecs = call->args.size();
        vector<Value *> args(num_vecs + 2);

        Type t = call->args[0].type();

        // Assume element-aligned.
        int alignment = t.bytes();

        // Codegen the lets
        for (size_t i = 0; i < lets.size(); i++) {
            sym_push(lets[i].first, codegen(lets[i].second));
        }

        // Codegen all the vector args
        args[0] = NULL;
        for (int i = 0; i < num_vecs; ++i) {
            args[i+1] = codegen(call->args[i]);
        }
        args.back() = ConstantInt::get(i32, alignment);

        // Grab the function
        std::ostringstream instr;
        instr << "llvm.arm.neon.vst" << num_vecs << ".v" << intrin_type.width;
        if (t.is_float()) {
            instr << "f" << t.bits;
        } else {
            instr << "i" << t.bits;
        }
        llvm::Function *fn = module->getFunction(instr.str());
        internal_assert(fn);

        // How many vst instructions do we need to generate?
        int slices = t.width / intrin_type.width;

        internal_assert(slices >= 1);
        for (int i = 0; i < t.width; i += intrin_type.width) {
            Expr slice_base = simplify(ramp->base + i * ramp->stride);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_type.width);
            Value *ptr = codegen_buffer_pointer(op->name, call->args[0].type().element_of(), slice_base);
            ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());

            vector<Value *> slice_args = args;
            slice_args[0] = ptr; // Set the pointer argument

            // Take a slice of each arg
            for (int j = 1; j < num_vecs + 1; j++) {
                slice_args[j] = slice_vector(slice_args[j], i, intrin_type.width);
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
    if (target.os != Target::NaCl) {
        ostringstream builtin;
        builtin << "strided_store_"
                << (op->value.type().is_float() ? 'f' : 'i')
                << op->value.type().bits
                << 'x' << op->value.type().width;

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->value.type().element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->value.type().bytes());
            Value *val = codegen(op->value);
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Instruction *store = builder->CreateCall(fn, vec(base, stride, val));
            (void)store;
            add_tbaa_metadata(store, op->name, op->index);
            return;
        }
    }

    CodeGen_Posix::visit(op);

}

void CodeGen_ARM::visit(const Load *op) {
    // AArch64 SIMD not yet supported
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

        Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), base);
        ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());

        // Decide what width to slice things into. If not a multiple
        // of 64 or 128 bits, then we can't safely slice it up into
        // some number of vlds, so we hand it over the base class.
        int bit_width = op->type.bits * op->type.width;
        int intrin_width = 0;
        if (bit_width % 128 == 0) {
            intrin_width = 128 / op->type.bits;
        } else if (bit_width % 64 == 0) {
            intrin_width = 64 / op->type.bits;
        } else {
            CodeGen_Posix::visit(op);
            return;
        }

        ostringstream intrin;
        intrin << "llvm.arm.neon.vld"
               << stride->value
               << ".v" << intrin_width
               << (op->type.is_float() ? 'f' : 'i')
               << op->type.bits;

        // Get the intrinsic
        llvm::Function *fn = module->getFunction(intrin.str());
        internal_assert(fn);

        // Load each slice.
        vector<Value *> results;
        for (int i = 0; i < op->type.width; i += intrin_width) {
            Expr slice_base = simplify(base + i*ramp->stride);
            Expr slice_ramp = Ramp::make(slice_base, ramp->stride, intrin_width);
            Value *ptr = codegen_buffer_pointer(op->name, op->type.element_of(), slice_base);
            ptr = builder->CreatePointerCast(ptr, i8->getPointerTo());
            CallInst *call = builder->CreateCall(fn, vec(ptr, align));
            add_tbaa_metadata(call, op->name, slice_ramp);

            Value *elt = builder->CreateExtractValue(call, vec((unsigned int)offset));
            results.push_back(elt);
        }

        // Concat the results
        value = concat_vectors(results);
        return;
    }

    // We have builtins for strided loads with fixed but unknown stride, but they use inline assembly.
    if (target.os != Target::NaCl) {
        ostringstream builtin;
        builtin << "strided_load_"
                << (op->type.is_float() ? 'f' : 'i')
                << op->type.bits
                << 'x' << op->type.width;

        llvm::Function *fn = module->getFunction(builtin.str());
        if (fn) {
            Value *base = codegen_buffer_pointer(op->name, op->type.element_of(), ramp->base);
            Value *stride = codegen(ramp->stride * op->type.bytes());
            debug(4) << "Creating call to " << builtin.str() << "\n";
            Instruction *load = builder->CreateCall(fn, vec(base, stride), builtin.str());
            add_tbaa_metadata(load, op->name, op->index);
            value = load;
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Call *op) {
    if (op->call_type == Call::Intrinsic) {
        if (op->name == Call::profiling_timer) {
            // Android devices generally have read-cycle-counter
            // disabled in user mode; fall back to calling
            // halide_current_time_ns().
            internal_assert(op->args.size() == 1);
            Expr e = Call::make(UInt(64), "halide_current_time_ns", std::vector<Expr>(), Call::Extern);
            e.accept(this);
            return;
        } else if (op->name == Call::abs && op->type.is_uint()) {
            internal_assert(op->args.size() == 1);
            // If the arg is a subtract with narrowable args, we can use vabdl.
            const Sub *sub = op->args[0].as<Sub>();
            if (sub) {
                Expr a = sub->a, b = sub->b;
                Type narrow = UInt(a.type().bits/2, a.type().width);
                Expr na = try_narrow(a, narrow);
                Expr nb = try_narrow(b, narrow);

                // Also try an unsigned narrowing
                if (!na.defined() || !nb.defined()) {
                    narrow = Int(narrow.bits, narrow.width);
                    na = try_narrow(a, narrow);
                    nb = try_narrow(b, narrow);
                }

                if (na.defined() && nb.defined()) {
                    Expr absd = Call::make(UInt(narrow.bits, narrow.width), Call::absd,
                                           vec(na, nb), Call::Intrinsic);

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
        return "";
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

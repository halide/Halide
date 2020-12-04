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

// Broadcast to an unknown number of lanes, for making patterns.
Expr bc(Expr x) {
    return Broadcast::make(std::move(x), 0);
}

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

    // Generate the cast patterns that can take vector types.
    Type types[] = {Int(8), UInt(8), Int(16), UInt(16), Int(32), UInt(32)};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        Type t = types[i].with_lanes(0);

        // Wider versions of the type
        Type w = t.widen();
        Type ws = Int(t.bits() * 2, t.lanes());

        // Vector wildcard for this type
        Expr vector = Variable::make(t, "*");
        Expr w_vector = Variable::make(w, "*");
        Expr ws_vector = Variable::make(ws, "*");

        // Bounds of the type stored in the wider vector type
        Expr tmin = simplify(cast(w, t.min()));
        Expr tmax = simplify(cast(w, t.max()));
        Expr tsmin = simplify(cast(ws, t.min()));
        Expr tsmax = simplify(cast(ws, t.max()));

        Pattern p("", Expr(), Pattern::NarrowArgs);
        p.intrin = "rhadd";
        p.pattern = cast(t, (w_vector + w_vector + 1) / 2);
        casts.push_back(p);
        p.pattern = cast(t, (w_vector + (w_vector + 1)) / 2);
        casts.push_back(p);
        p.pattern = cast(t, ((w_vector + 1) + w_vector) / 2);
        casts.push_back(p);

        // Rounding down averaging
        p.intrin = "hadd";
        p.pattern = cast(t, (w_vector + w_vector) / 2);
        casts.push_back(p);

        // Halving subtract
        p.intrin = "hsub";
        p.pattern = cast(t, (w_vector - w_vector) / 2);
        casts.push_back(p);

        // Saturating add
        p.intrin = "addsat";
        p.pattern = cast(t, clamp(w_vector + w_vector, tmin, tmax));
        casts.push_back(p);

        // In the unsigned case, the saturation below is unnecessary
        if (t.is_uint()) {
            p.pattern = cast(t, min(w_vector + w_vector, tmax));
            casts.push_back(p);
        }

        // Saturating subtract
        p.intrin = "subsat";
        p.pattern = cast(t, clamp(ws_vector - ws_vector, tsmin, tsmax));
        casts.push_back(p);

        // In the unsigned case, we may detect that the top of the clamp is unnecessary
        if (t.is_uint()) {
            p.pattern = cast(t, max(ws_vector - ws_vector, 0));
            casts.push_back(p);
        }
    }

    casts.emplace_back("qrdmulh", i16_sat((wild_i32x_ * wild_i32x_ + (1 << 14)) / (1 << 15)), Pattern::NarrowArgs);
    casts.emplace_back("qrdmulh", i32_sat((wild_i64x_ * wild_i64x_ + (1 << 30)) / Expr(int64_t(1) << 31)), Pattern::NarrowArgs);

    casts.emplace_back("qshrn", i8_sat(wild_i16x_ / bc(wild_i16_)), Pattern::RightShift);
    casts.emplace_back("qshrn", i16_sat(wild_i32x_ / bc(wild_i32_)), Pattern::RightShift);
    casts.emplace_back("qshrn", i32_sat(wild_i64x_ / bc(wild_i64_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u8_sat(wild_u16x_ / bc(wild_u16_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u16_sat(wild_u32x_ / bc(wild_u32_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u32_sat(wild_u64x_ / bc(wild_u64_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u8_sat(wild_i16x_ / bc(wild_i16_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u16_sat(wild_i32x_ / bc(wild_i32_)), Pattern::RightShift);
    casts.emplace_back("qshrn", u32_sat(wild_i64x_ / bc(wild_i64_)), Pattern::RightShift);

    casts.emplace_back("qshl", i8_sat(i16(wild_i8x_) * wild_i16x_), Pattern::LeftShift);
    casts.emplace_back("qshl", i16_sat(i32(wild_i16x_) * wild_i32x_), Pattern::LeftShift);
    casts.emplace_back("qshl", i32_sat(i64(wild_i32x_) * wild_i64x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u8_sat(u16(wild_u8x_) * wild_u16x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u16_sat(u32(wild_u16x_) * wild_u32x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u32_sat(u64(wild_u32x_) * wild_u64x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u8_sat(i16(wild_i8x_) * wild_i16x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u16_sat(i32(wild_i16x_) * wild_i32x_), Pattern::LeftShift);
    casts.emplace_back("qshl", u32_sat(i64(wild_i32x_) * wild_i64x_), Pattern::LeftShift);

    casts.emplace_back("qxtn", i8_sat(wild_i16x_));
    casts.emplace_back("qxtn", i16_sat(wild_i32x_));
    casts.emplace_back("qxtn", i32_sat(wild_i64x_));
    casts.emplace_back("qxtn", u8_sat(wild_u16x_));
    casts.emplace_back("qxtn", u16_sat(wild_u32x_));
    casts.emplace_back("qxtn", u32_sat(wild_u64x_));
    casts.emplace_back("qxtn", u8_sat(wild_i16x_));
    casts.emplace_back("qxtn", u16_sat(wild_i32x_));
    casts.emplace_back("qxtn", u32_sat(wild_i64x_));

    // Overflow for int32 is not defined by Halide, so for those we can take
    // advantage of special add-and-halve instructions.
    //
    // 128-bit
    averagings.emplace_back("hadd", (wild_i32x_ + wild_i32x_));

    // 128-bit
    averagings.emplace_back("hsub", (wild_i32x_ - wild_i32x_));

    // 128-bit
    negations.emplace_back("qneg", -max(wild_i8x_, -127));
    negations.emplace_back("qneg", -max(wild_i16x_, -32767));
    negations.emplace_back("qneg", -max(wild_i32x_, -(0x7fffffff)));

    // Widening multiplies.
    multiplies.emplace_back("mull", wild_i16x_ * wild_i16x_, Pattern::NarrowArgs);
    multiplies.emplace_back("mull", wild_u16x_ * wild_u16x_, Pattern::NarrowArgs);
    multiplies.emplace_back("mull", wild_i32x_ * wild_i32x_, Pattern::NarrowArgs);
    multiplies.emplace_back("mull", wild_u32x_ * wild_u32x_, Pattern::NarrowArgs);
    multiplies.emplace_back("mull", wild_i64x_ * wild_i64x_, Pattern::NarrowArgs);
    multiplies.emplace_back("mull", wild_u64x_ * wild_u64x_, Pattern::NarrowArgs);
}


namespace {

struct ArmIntrinsic {
    const char *arm32;
    const char *arm64;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[4];
    int flags;
};

// TODO: these should probably be declared constexpr, but that would
// require marking various halide_type_t methods as constexpr, and an
// obscure bug in MSVC2017 can cause compilation failures for them.
// The bug appears to be fixed in MSVC2019, so when we move to that
// as a baseline for Windows, this should be revisited.
halide_type_t i8 = halide_type_t(halide_type_int, 8);
halide_type_t i16 = halide_type_t(halide_type_int, 16);
halide_type_t i32 = halide_type_t(halide_type_int, 32);
halide_type_t i64 = halide_type_t(halide_type_int, 64);
halide_type_t u8 = halide_type_t(halide_type_uint, 8);
halide_type_t u16 = halide_type_t(halide_type_uint, 16);
halide_type_t u32 = halide_type_t(halide_type_uint, 32);
halide_type_t u64 = halide_type_t(halide_type_uint, 64);
halide_type_t f32 = halide_type_t(halide_type_float, 32);

// Define vectors that are 1/2x and 1x the ARM width
constexpr int kNeonWidth = 128;
constexpr int kHalfNeonWidth = kNeonWidth / 2;

halide_type_t i8v1 = i8.with_lanes(kHalfNeonWidth / 8);
halide_type_t i16v1 = i16.with_lanes(kHalfNeonWidth / 16);
halide_type_t i32v1 = i32.with_lanes(kHalfNeonWidth / 32);
halide_type_t i64v1 = i64.with_lanes(kHalfNeonWidth / 64);
halide_type_t u8v1 = u8.with_lanes(kHalfNeonWidth / 8);
halide_type_t u16v1 = u16.with_lanes(kHalfNeonWidth / 16);
halide_type_t u32v1 = u32.with_lanes(kHalfNeonWidth / 32);
halide_type_t u64v1 = u64.with_lanes(kHalfNeonWidth / 64);
halide_type_t f32v1 = f32.with_lanes(kHalfNeonWidth / 32);

halide_type_t i8v2 = i8v1.with_lanes(i8v1.lanes * 2);
halide_type_t i16v2 = i16v1.with_lanes(i16v1.lanes * 2);
halide_type_t i32v2 = i32v1.with_lanes(i32v1.lanes * 2);
halide_type_t i64v2 = i64v1.with_lanes(i64v1.lanes * 2);
halide_type_t u8v2 = u8v1.with_lanes(u8v1.lanes * 2);
halide_type_t u16v2 = u16v1.with_lanes(u16v1.lanes * 2);
halide_type_t u32v2 = u32v1.with_lanes(u32v1.lanes * 2);
halide_type_t u64v2 = u64v1.with_lanes(u64v1.lanes * 2);
halide_type_t f32v2 = f32v1.with_lanes(f32v1.lanes * 2);

// clang-format off
const ArmIntrinsic intrinsic_defs[] = {
    // Widening multiply
    {"vmulls.v8i16", "smull.v8i16", i16v2, "mull", {i8v1, i8v1}},
    {"vmullu.v8i16", "umull.v8i16", u16v2, "mull", {u8v1, u8v1}},
    {"vmulls.v4i32", "smull.v4i32", i32v2, "mull", {i16v1, i16v1}},
    {"vmullu.v4i32", "umull.v4i32", u32v2, "mull", {u16v1, u16v1}},
    {"vmulls.v2i64", "smull.v2i64", i64v2, "mull", {i32v1, i32v1}},
    {"vmullu.v2i64", "umull.v2i64", u64v2, "mull", {u32v1, u32v1}},

    // Saturating add
    {"llvm.sadd.sat.v8i8", "llvm.sadd.sat.v8i8", i8v1, "addsat", {i8v1, i8v1}},
    {"llvm.uadd.sat.v8i8", "llvm.uadd.sat.v8i8", u8v1, "addsat", {u8v1, u8v1}},
    {"llvm.sadd.sat.v4i16", "llvm.sadd.sat.v4i16", i16v1, "addsat", {i16v1, i16v1}},
    {"llvm.uadd.sat.v4i16", "llvm.uadd.sat.v4i16", u16v1, "addsat", {u16v1, u16v1}},
    {"llvm.sadd.sat.v2i32", "llvm.sadd.sat.v2i32", i32v1, "addsat", {i32v1, i32v1}},
    {"llvm.uadd.sat.v2i32", "llvm.uadd.sat.v2i32", u32v1, "addsat", {u32v1, u32v1}},

    {"llvm.sadd.sat.v16i8", "llvm.sadd.sat.v16i8", i8v2, "addsat", {i8v2, i8v2}},
    {"llvm.uadd.sat.v16i8", "llvm.uadd.sat.v16i8", u8v2, "addsat", {u8v2, u8v2}},
    {"llvm.sadd.sat.v8i16", "llvm.sadd.sat.v8i16", i16v2, "addsat", {i16v2, i16v2}},
    {"llvm.uadd.sat.v8i16", "llvm.uadd.sat.v8i16", u16v2, "addsat", {u16v2, u16v2}},
    {"llvm.sadd.sat.v4i32", "llvm.sadd.sat.v4i32", i32v2, "addsat", {i32v2, i32v2}},
    {"llvm.uadd.sat.v4i32", "llvm.uadd.sat.v4i32", u32v2, "addsat", {u32v2, u32v2}},

    // Saturating subtract
    {"llvm.ssub.sat.v8i8", "llvm.ssub.sat.v8i8", i8v1, "subsat", {i8v1, i8v1}},
    {"llvm.usub.sat.v8i8", "llvm.usub.sat.v8i8", u8v1, "subsat", {u8v1, u8v1}},
    {"llvm.ssub.sat.v4i16", "llvm.ssub.sat.v4i16", i16v1, "subsat", {i16v1, i16v1}},
    {"llvm.usub.sat.v4i16", "llvm.usub.sat.v4i16", u16v1, "subsat", {u16v1, u16v1}},
    {"llvm.ssub.sat.v2i32", "llvm.ssub.sat.v2i32", i32v1, "subsat", {i32v1, i32v1}},
    {"llvm.usub.sat.v2i32", "llvm.usub.sat.v2i32", u32v1, "subsat", {u32v1, u32v1}},

    {"llvm.ssub.sat.v16i8", "llvm.ssub.sat.v16i8", i8v2, "subsat", {i8v2, i8v2}},
    {"llvm.usub.sat.v16i8", "llvm.usub.sat.v16i8", u8v2, "subsat", {u8v2, u8v2}},
    {"llvm.ssub.sat.v8i16", "llvm.ssub.sat.v8i16", i16v2, "subsat", {i16v2, i16v2}},
    {"llvm.usub.sat.v8i16", "llvm.usub.sat.v8i16", u16v2, "subsat", {u16v2, u16v2}},
    {"llvm.ssub.sat.v4i32", "llvm.ssub.sat.v4i32", i32v2, "subsat", {i32v2, i32v2}},
    {"llvm.usub.sat.v4i32", "llvm.usub.sat.v4i32", u32v2, "subsat", {u32v2, u32v2}},

    // Halving add
    {"vhadds.v8i8", "shadd.v8i8", i8v1, "hadd", {i8v1, i8v1}},
    {"vhaddu.v8i8", "uhadd.v8i8", u8v1, "hadd", {u8v1, u8v1}},
    {"vhadds.v4i16", "shadd.v4i16", i16v1, "hadd", {i16v1, i16v1}},
    {"vhaddu.v4i16", "uhadd.v4i16", u16v1, "hadd", {u16v1, u16v1}},
    {"vhadds.v2i32", "shadd.v2i32", i32v1, "hadd", {i32v1, i32v1}},
    {"vhaddu.v2i32", "uhadd.v2i32", u32v1, "hadd", {u32v1, u32v1}},

    {"vhadds.v16i8", "shadd.v16i8", i8v2, "hadd", {i8v2, i8v2}},
    {"vhaddu.v16i8", "uhadd.v16i8", u8v2, "hadd", {u8v2, u8v2}},
    {"vhadds.v8i16", "shadd.v8i16", i16v2, "hadd", {i16v2, i16v2}},
    {"vhaddu.v8i16", "uhadd.v8i16", u16v2, "hadd", {u16v2, u16v2}},
    {"vhadds.v4i32", "shadd.v4i32", i32v2, "hadd", {i32v2, i32v2}},
    {"vhaddu.v4i32", "uhadd.v4i32", u32v2, "hadd", {u32v2, u32v2}},

    // Halving subtract
    {"vhsubs.v8i8", "shsub.v8i8", i8v1, "hsub", {i8v1, i8v1}},
    {"vhsubu.v8i8", "uhsub.v8i8", u8v1, "hsub", {u8v1, u8v1}},
    {"vhsubs.v4i16", "shsub.v4i16", i16v1, "hsub", {i16v1, i16v1}},
    {"vhsubu.v4i16", "uhsub.v4i16", u16v1, "hsub", {u16v1, u16v1}},
    {"vhsubs.v2i32", "shsub.v2i32", i32v1, "hsub", {i32v1, i32v1}},
    {"vhsubu.v2i32", "uhsub.v2i32", u32v1, "hsub", {u32v1, u32v1}},

    {"vhsubs.v16i8", "shsub.v16i8", i8v2, "hsub", {i8v2, i8v2}},
    {"vhsubu.v16i8", "uhsub.v16i8", u8v2, "hsub", {u8v2, u8v2}},
    {"vhsubs.v8i16", "shsub.v8i16", i16v2, "hsub", {i16v2, i16v2}},
    {"vhsubu.v8i16", "uhsub.v8i16", u16v2, "hsub", {u16v2, u16v2}},
    {"vhsubs.v4i32", "shsub.v4i32", i32v2, "hsub", {i32v2, i32v2}},
    {"vhsubu.v4i32", "uhsub.v4i32", u32v2, "hsub", {u32v2, u32v2}},

    // Halving add rounding
    {"vrhadds.v8i8", "srhadd.v8i8", i8v1, "rhadd", {i8v1, i8v1}},
    {"vrhaddu.v8i8", "urhadd.v8i8", u8v1, "rhadd", {u8v1, u8v1}},
    {"vrhadds.v4i16", "srhadd.v4i16", i16v1, "rhadd", {i16v1, i16v1}},
    {"vrhaddu.v4i16", "urhadd.v4i16", u16v1, "rhadd", {u16v1, u16v1}},
    {"vrhadds.v2i32", "srhadd.v2i32", i32v1, "rhadd", {i32v1, i32v1}},
    {"vrhaddu.v2i32", "urhadd.v2i32", u32v1, "rhadd", {u32v1, u32v1}},

    {"vrhadds.v16i8", "srhadd.v16i8", i8v2, "rhadd", {i8v2, i8v2}},
    {"vrhaddu.v16i8", "urhadd.v16i8", u8v2, "rhadd", {u8v2, u8v2}},
    {"vrhadds.v8i16", "srhadd.v8i16", i16v2, "rhadd", {i16v2, i16v2}},
    {"vrhaddu.v8i16", "urhadd.v8i16", u16v2, "rhadd", {u16v2, u16v2}},
    {"vrhadds.v4i32", "srhadd.v4i32", i32v2, "rhadd", {i32v2, i32v2}},
    {"vrhaddu.v4i32", "urhadd.v4i32", u32v2, "rhadd", {u32v2, u32v2}},

    // Min
    {"vmins.v8i8", "smin.v8i8", i8v1, "min", {i8v1, i8v1}},
    {"vminu.v8i8", "umin.v8i8", u8v1, "min", {u8v1, u8v1}},
    {"vmins.v4i16", "smin.v4i16", i16v1, "min", {i16v1, i16v1}},
    {"vminu.v4i16", "umin.v4i16", u16v1, "min", {u16v1, u16v1}},
    {"vmins.v2i32", "smin.v2i32", i32v1, "min", {i32v1, i32v1}},
    {"vminu.v2i32", "umin.v2i32", u32v1, "min", {u32v1, u32v1}},
    {"vmins.v2f32", "fmin.v2f32", f32v1, "min", {f32v1, f32v1}},

    {"vmins.v16i8", "smin.v16i8", i8v2, "min", {i8v2, i8v2}},
    {"vminu.v16i8", "umin.v16i8", u8v2, "min", {u8v2, u8v2}},
    {"vmins.v8i16", "smin.v8i16", i16v2, "min", {i16v2, i16v2}},
    {"vminu.v8i16", "umin.v8i16", u16v2, "min", {u16v2, u16v2}},
    {"vmins.v4i32", "smin.v4i32", i32v2, "min", {i32v2, i32v2}},
    {"vminu.v4i32", "umin.v4i32", u32v2, "min", {u32v2, u32v2}},
    {"vmins.v4f32", "fmin.v4f32", f32v2, "min", {f32v2, f32v2}},

    // Max
    {"vmaxs.v8i8", "smax.v8i8", i8v1, "max", {i8v1, i8v1}},
    {"vmaxu.v8i8", "umax.v8i8", u8v1, "max", {u8v1, u8v1}},
    {"vmaxs.v4i16", "smax.v4i16", i16v1, "max", {i16v1, i16v1}},
    {"vmaxu.v4i16", "umax.v4i16", u16v1, "max", {u16v1, u16v1}},
    {"vmaxs.v2i32", "smax.v2i32", i32v1, "max", {i32v1, i32v1}},
    {"vmaxu.v2i32", "umax.v2i32", u32v1, "max", {u32v1, u32v1}},
    {"vmaxs.v2f32", "fmax.v2f32", f32v1, "max", {f32v1, f32v1}},

    {"vmaxs.v16i8", "smax.v16i8", i8v2, "max", {i8v2, i8v2}},
    {"vmaxu.v16i8", "umax.v16i8", u8v2, "max", {u8v2, u8v2}},
    {"vmaxs.v8i16", "smax.v8i16", i16v2, "max", {i16v2, i16v2}},
    {"vmaxu.v8i16", "umax.v8i16", u16v2, "max", {u16v2, u16v2}},
    {"vmaxs.v4i32", "smax.v4i32", i32v2, "max", {i32v2, i32v2}},
    {"vmaxu.v4i32", "umax.v4i32", u32v2, "max", {u32v2, u32v2}},
    {"vmaxs.v4f32", "fmax.v4f32", f32v2, "max", {f32v2, f32v2}},

    // Saturating negation
    {"vqneg.v8i8", "sqneg.v8i8", i8v1, "qneg", {i8v1}},
    {"vqneg.v4i16", "sqneg.v4i16", i16v1, "qneg", {i16v1}},
    {"vqneg.v2i32", "sqneg.v2i32", i32v1, "qneg", {i32v1}},

    {"vqneg.v16i8", "sqneg.v16i8", i8v2, "qneg", {i8v2}},
    {"vqneg.v8i16", "sqneg.v8i16", i16v2, "qneg", {i16v2}},
    {"vqneg.v4i32", "sqneg.v4i32", i32v2, "qneg", {i32v2}},

    // Saturating narrowing
    {"vqmovns.v8i8", "sqxtn.v8i8", i8v1, "qxtn", {i16v2}},
    {"vqmovnu.v8i8", "uqxtn.v8i8", u8v1, "qxtn", {u16v2}},
    {"vqmovns.v4i16", "sqxtn.v4i16", i16v1, "qxtn", {i32v2}},
    {"vqmovnu.v4i16", "uqxtn.v4i16", u16v1, "qxtn", {u32v2}},
    {"vqmovns.v2i32", "sqxtn.v2i32", i32v1, "qxtn", {i64v2}},
    {"vqmovnu.v2i32", "uqxtn.v2i32", u32v1, "qxtn", {u64v2}},
    {"vqmovnsu.v8i8", "sqxtun.v8i8", u8v1, "qxtn", {i16v2}},
    {"vqmovnsu.v4i16", "sqxtun.v4i16", u16v1, "qxtn", {i32v2}},
    {"vqmovnsu.v2i32", "sqxtun.v2i32", u32v1, "qxtn", {i64v2}},

    // Saturating shift left by signed register
    // TODO: There's also qshl by a scalar immediate we should target.
    // TODO: Rather than duplicating this part of the table so many times, maybe we should
    // allow call_elementwise_intrinsic to cast as needed.
    {"vqshifts.v8i8", "sqshl.v8i8", i8v1, "qshl", {i8v1, i8v1}},
    {"vqshiftu.v8i8", "uqshl.v8i8", u8v1, "qshl", {u8v1, i8v1}},
    {"vqshifts.v4i16", "sqshl.v4i16", i16v1, "qshl", {i16v1, i16v1}},
    {"vqshiftu.v4i16", "uqshl.v4i16", u16v1, "qshl", {u16v1, i16v1}},
    {"vqshifts.v2i32", "sqshl.v2i32", i32v1, "qshl", {i32v1, i32v1}},
    {"vqshiftu.v2i32", "uqshl.v2i32", u32v1, "qshl", {u32v1, i32v1}},
    {"vqshiftsu.v8i8", "sqshlu.v8i8", u8v1, "qshl", {i8v1, i8v1}},
    {"vqshiftsu.v4i16", "sqshlu.v4i16", u16v1, "qshl", {i16v1, i16v1}},
    {"vqshiftsu.v2i32", "sqshlu.v2i32", u32v1, "qshl", {i32v1, i32v1}},

    {"vqshifts.v16i8", "sqshl.v16i8", i8v2, "qshl", {i8v2, i8v2}},
    {"vqshiftu.v16i8", "uqshl.v16i8", u8v2, "qshl", {u8v2, i8v2}},
    {"vqshifts.v8i16", "sqshl.v8i16", i16v2, "qshl", {i16v2, i16v2}},
    {"vqshiftu.v8i16", "uqshl.v8i16", u16v2, "qshl", {u16v2, i16v2}},
    {"vqshifts.v4i32", "sqshl.v4i32", i32v2, "qshl", {i32v2, i32v2}},
    {"vqshiftu.v4i32", "uqshl.v4i32", u32v2, "qshl", {u32v2, i32v2}},
    {"vqshiftsu.v16i8", "sqshlu.v16i8", u8v2, "qshl", {i8v2, i8v2}},
    {"vqshiftsu.v8i16", "sqshlu.v8i16", u16v2, "qshl", {i16v2, i16v2}},
    {"vqshiftsu.v4i32", "sqshlu.v4i32", u32v2, "qshl", {i32v2, i32v2}},

    // Saturating shift left by unsigned register
    {"vqshifts.v8i8", "sqshl.v8i8", i8v1, "qshl", {i8v1, u8v1}},
    {"vqshiftu.v8i8", "uqshl.v8i8", u8v1, "qshl", {u8v1, u8v1}},
    {"vqshifts.v4i16", "sqshl.v4i16", i16v1, "qshl", {i16v1, u16v1}},
    {"vqshiftu.v4i16", "uqshl.v4i16", u16v1, "qshl", {u16v1, u16v1}},
    {"vqshifts.v2i32", "sqshl.v2i32", i32v1, "qshl", {i32v1, u32v1}},
    {"vqshiftu.v2i32", "uqshl.v2i32", u32v1, "qshl", {u32v1, u32v1}},
    {"vqshiftsu.v8i8", "sqshlu.v8i8", u8v1, "qshl", {i8v1, u8v1}},
    {"vqshiftsu.v4i16", "sqshlu.v4i16", u16v1, "qshl", {i16v1, u16v1}},
    {"vqshiftsu.v2i32", "sqshlu.v2i32", u32v1, "qshl", {i32v1, u32v1}},

    {"vqshifts.v16i8", "sqshl.v16i8", i8v2, "qshl", {i8v2, u8v2}},
    {"vqshiftu.v16i8", "uqshl.v16i8", u8v2, "qshl", {u8v2, u8v2}},
    {"vqshifts.v8i16", "sqshl.v8i16", i16v2, "qshl", {i16v2, u16v2}},
    {"vqshiftu.v8i16", "uqshl.v8i16", u16v2, "qshl", {u16v2, u16v2}},
    {"vqshifts.v4i32", "sqshl.v4i32", i32v2, "qshl", {i32v2, u32v2}},
    {"vqshiftu.v4i32", "uqshl.v4i32", u32v2, "qshl", {u32v2, u32v2}},
    {"vqshiftsu.v16i8", "sqshlu.v16i8", u8v2, "qshl", {i8v2, u8v2}},
    {"vqshiftsu.v8i16", "sqshlu.v8i16", u16v2, "qshl", {i16v2, u16v2}},
    {"vqshiftsu.v4i32", "sqshlu.v4i32", u32v2, "qshl", {i32v2, u32v2}},

    // Saturating narrowing shift right by an immediate
    {"vqshiftns.v8i8", "sqshrn.v8i8", i8v1, "qshrn", {i16v2, u32}},
    {"vqshiftnu.v8i8", "uqshrn.v8i8", u8v1, "qshrn", {u16v2, u32}},
    {"vqshiftns.v4i16", "sqshrn.v4i16", i16v1, "qshrn", {i32v2, u32}},
    {"vqshiftnu.v4i16", "uqshrn.v4i16", u16v1, "qshrn", {u32v2, u32}},
    {"vqshiftns.v2i32", "sqshrn.v2i32", i32v1, "qshrn", {i64v2, u32}},
    {"vqshiftnu.v2i32", "uqshrn.v2i32", u32v1, "qshrn", {u64v2, u32}},
    {"vqshiftnsu.v8i8", "sqshrun.v8i8", u8v1, "qshrn", {i16v2, u32}},
    {"vqshiftnsu.v4i16", "sqshrun.v4i16", u16v1, "qshrn", {i32v2, u32}},
    {"vqshiftnsu.v2i32", "sqshrun.v2i32", u32v1, "qshrn", {i64v2, u32}},

    // Saturating doubling multiply keep high half.
    {"vqdmulh.v4i16", "sqdmulh.v4i16", i16v1, "qdmulh", {i16v1, i16v1}},
    {"vqdmulh.v2i32", "sqdmulh.v2i32", i32v1, "qdmulh", {i32v1, i32v1}},

    {"vqdmulh.v8i16", "sqdmulh.v8i16", i16v2, "qdmulh", {i16v2, i16v2}},
    {"vqdmulh.v4i32", "sqdmulh.v4i32", i32v2, "qdmulh", {i32v2, i32v2}},

    // Saturating doubling multiply keep high half with rounding.
    {"vqrdmulh.v4i16", "sqrdmulh.v4i16", i16v1, "qrdmulh", {i16v1, i16v1}},
    {"vqrdmulh.v2i32", "sqrdmulh.v2i32", i32v1, "qrdmulh", {i32v1, i32v1}},

    {"vqrdmulh.v8i16", "sqrdmulh.v8i16", i16v2, "qrdmulh", {i16v2, i16v2}},
    {"vqrdmulh.v4i32", "sqrdmulh.v4i32", i32v2, "qrdmulh", {i32v2, i32v2}},
};
// clang-format on

}  // namespace


void CodeGen_ARM::init_module() {
    CodeGen_Posix::init_module();

    std::string prefix = target.bits == 32 ? "llvm.arm.neon." : "llvm.aarch64.neon.";

    for (const ArmIntrinsic &i : intrinsic_defs) {
        std::string intrin_name;
        if (target.bits == 32) {
            intrin_name = i.arm32;
        } else {
            intrin_name = i.arm64;
        }

        if (!starts_with(intrin_name, "llvm.")) {
            intrin_name = prefix + intrin_name;
        }

        Type ret_type = i.ret_type;
        std::vector<Type> arg_types;
        arg_types.reserve(4);
        for (halide_type_t i : i.arg_types) {
            if (i.bits == 0) {
                break;
            }
            arg_types.push_back(i);
        }

        llvm::Function *intrin = module->getFunction(intrin_name);
        if (!intrin) {
            vector<llvm::Type *> llvm_arg_types(arg_types.size());
            for (size_t i = 0; i < arg_types.size(); i++) {
                llvm_arg_types[i] = llvm_type_of(arg_types[i]);
            }

            llvm::Type *llvm_ret_type = llvm_type_of(ret_type);
            FunctionType *func_t = FunctionType::get(llvm_ret_type, llvm_arg_types, false);
            intrin = llvm::Function::Create(func_t, llvm::Function::ExternalLinkage, intrin_name, module.get());
            intrin->setCallingConv(CallingConv::C);
        }

        intrinsics[i.name].emplace_back(ret_type, std::move(arg_types), intrin);
    }
}

Value *CodeGen_ARM::call_pattern(const Pattern &p, Type t, const vector<Expr> &args) {
    return call_elementwise_intrinsic(t, p.intrin, args);
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
            } else {  // must be a shift
                Expr constant = matches[1];
                int shift_amount;
                bool power_of_two = is_const_power_of_two_integer(constant, &shift_amount);
                if (power_of_two && shift_amount < matches[0].type().bits()) {
                    if (target.bits == 32 && pattern.type == Pattern::RightShift) {
                        // The arm32 llvm backend wants right shifts to come in as negative values.
                        shift_amount = -shift_amount;
                    }
                    Expr b;
                    if (matches[1].type().is_scalar()) {
                        b = make_const(UInt(32), shift_amount);
                    } else {
                        b = make_const(Int(matches[0].type().bits(), matches[0].type().lanes()), shift_amount);
                    }
                    value = call_pattern(pattern, t, {matches[0], b});
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

    Type t = op->type;
    vector<Expr> matches;

    int shift_amount = 0;
    if (is_const_power_of_two_integer(op->b, &shift_amount)) {
        // Let LLVM handle these.
        CodeGen_Posix::visit(op);
        return;
    }

    // LLVM really struggles to generate mlal unless we generate mull intrinsics
    // for the multiplication part first.
    for (size_t i = 0; i < multiplies.size(); i++) {
        const Pattern &pattern = multiplies[i];
        //debug(4) << "Trying pattern: " << patterns[i].intrin << " " << patterns[i].pattern << "\n";
        if (expr_match(pattern.pattern, op, matches)) {

            //debug(4) << "Match!\n";
            if (pattern.type == Pattern::Simple) {
                value = call_pattern(pattern, t, matches);
                return;
            } else if (pattern.type == Pattern::NarrowArgs) {
                Type narrow_t = t.narrow();
                // Try to narrow all of the args.
                bool all_narrow = true;
                for (size_t i = 0; i < matches.size(); i++) {
                    internal_assert(matches[i].type().bits() == t.bits());
                    internal_assert(matches[i].type().lanes() == t.lanes());
                    // debug(4) << "Attemping to narrow " << matches[i] << " to " << t << "\n";
                    matches[i] = lossless_cast(narrow_t, matches[i]);
                    if (!matches[i].defined()) {
                        // debug(4) << "failed\n";
                        all_narrow = false;
                    } else {
                        // debug(4) << "success: " << matches[i] << "\n";
                        internal_assert(matches[i].type() == narrow_t);
                    }
                }

                if (all_narrow) {
                    value = call_pattern(pattern, t, matches);
                    return;
                }
            }
        }
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

void CodeGen_ARM::visit(const Div *op) {
    if (!neon_intrinsics_disabled() &&
        op->type.is_vector() && is_const(op->b, 2) &&
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
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type == Float(32) || op->type.is_vector())) {
        value = call_elementwise_intrinsic(op->type, "min", {op->a, op->b});
        if (value) {
            return;
        }
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_ARM::visit(const Max *op) {
    // Use a 2-wide vector for scalar floats.
    if (!neon_intrinsics_disabled() && (op->type == Float(32) || op->type.is_vector())) {
        value = call_elementwise_intrinsic(op->type, "max", {op->a, op->b});
        if (value) {
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

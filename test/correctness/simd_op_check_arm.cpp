#include "simd_op_check.h"

#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

using namespace std::string_literals;

class SimdOpCheckARM : public SimdOpCheckTest {
public:
    SimdOpCheckARM(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }

    void add_tests() override {
        if (target.arch == Target::ARM) {
            check_neon_all();
        }
    }

    void check_neon_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f16_1 = in_f16(x), f16_2 = in_f16(x + 16), f16_3 = in_f16(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32), i8_4 = in_i8(x + 48);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32), u8_4 = in_u8(x + 48);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // Table copied from the Cortex-A9 TRM.

        // In general neon ops have the 64-bit version, the 128-bit
        // version (ending in q), and the widening version that takes
        // 64-bit args and produces a 128-bit result (ending in l). We try
        // to peephole match any with vector, so we just try 64-bits, 128
        // bits, 192 bits, and 256 bits for everything.

        bool arm32 = (target.bits == 32);

        for (int w = 1; w <= 4; w++) {

            // VABA     I       -       Absolute Difference and Accumulate
            check(arm32 ? "vaba.s8" : "saba", 8 * w, i8_1 + absd(i8_2, i8_3));
            check(arm32 ? "vaba.u8" : "uaba", 8 * w, u8_1 + absd(u8_2, u8_3));
            check(arm32 ? "vaba.s16" : "saba", 4 * w, i16_1 + absd(i16_2, i16_3));
            check(arm32 ? "vaba.u16" : "uaba", 4 * w, u16_1 + absd(u16_2, u16_3));
            check(arm32 ? "vaba.s32" : "saba", 2 * w, i32_1 + absd(i32_2, i32_3));
            check(arm32 ? "vaba.u32" : "uaba", 2 * w, u32_1 + absd(u32_2, u32_3));

            // VABAL    I       -       Absolute Difference and Accumulate Long
            check(arm32 ? "vabal.s8" : "sabal", 8 * w, i16_1 + absd(i8_2, i8_3));
            check(arm32 ? "vabal.u8" : "uabal", 8 * w, u16_1 + absd(u8_2, u8_3));
            check(arm32 ? "vabal.s16" : "sabal", 4 * w, i32_1 + absd(i16_2, i16_3));
            check(arm32 ? "vabal.u16" : "uabal", 4 * w, u32_1 + absd(u16_2, u16_3));
            check(arm32 ? "vabal.s32" : "sabal", 2 * w, i64_1 + absd(i32_2, i32_3));
            check(arm32 ? "vabal.u32" : "uabal", 2 * w, u64_1 + absd(u32_2, u32_3));

            // VABD     I, F    -       Absolute Difference
            check(arm32 ? "vabd.s8" : "sabd", 8 * w, absd(i8_2, i8_3));
            check(arm32 ? "vabd.u8" : "uabd", 8 * w, absd(u8_2, u8_3));
            check(arm32 ? "vabd.s16" : "sabd", 4 * w, absd(i16_2, i16_3));
            check(arm32 ? "vabd.u16" : "uabd", 4 * w, absd(u16_2, u16_3));
            check(arm32 ? "vabd.s32" : "sabd", 2 * w, absd(i32_2, i32_3));
            check(arm32 ? "vabd.u32" : "uabd", 2 * w, absd(u32_2, u32_3));

            // Via widening, taking abs, then narrowing
            check(arm32 ? "vabd.s8" : "sabd", 8 * w, u8(abs(i16(i8_2) - i8_3)));
            check(arm32 ? "vabd.u8" : "uabd", 8 * w, u8(abs(i16(u8_2) - u8_3)));
            check(arm32 ? "vabd.s16" : "sabd", 4 * w, u16(abs(i32(i16_2) - i16_3)));
            check(arm32 ? "vabd.u16" : "uabd", 4 * w, u16(abs(i32(u16_2) - u16_3)));
            check(arm32 ? "vabd.s32" : "sabd", 2 * w, u32(abs(i64(i32_2) - i32_3)));
            check(arm32 ? "vabd.u32" : "uabd", 2 * w, u32(abs(i64(u32_2) - u32_3)));

            // VABDL    I       -       Absolute Difference Long
            check(arm32 ? "vabdl.s8" : "sabdl", 8 * w, i16(absd(i8_2, i8_3)));
            check(arm32 ? "vabdl.u8" : "uabdl", 8 * w, u16(absd(u8_2, u8_3)));
            check(arm32 ? "vabdl.s16" : "sabdl", 4 * w, i32(absd(i16_2, i16_3)));
            check(arm32 ? "vabdl.u16" : "uabdl", 4 * w, u32(absd(u16_2, u16_3)));
            check(arm32 ? "vabdl.s32" : "sabdl", 2 * w, i64(absd(i32_2, i32_3)));
            check(arm32 ? "vabdl.u32" : "uabdl", 2 * w, u64(absd(u32_2, u32_3)));

            // Via widening then taking an abs
            check(arm32 ? "vabdl.s8" : "sabdl", 8 * w, abs(i16(i8_2) - i16(i8_3)));
            check(arm32 ? "vabdl.u8" : "uabdl", 8 * w, abs(i16(u8_2) - i16(u8_3)));
            check(arm32 ? "vabdl.s16" : "sabdl", 4 * w, abs(i32(i16_2) - i32(i16_3)));
            check(arm32 ? "vabdl.u16" : "uabdl", 4 * w, abs(i32(u16_2) - i32(u16_3)));
            check(arm32 ? "vabdl.s32" : "sabdl", 2 * w, abs(i64(i32_2) - i64(i32_3)));
            check(arm32 ? "vabdl.u32" : "uabdl", 2 * w, abs(i64(u32_2) - i64(u32_3)));

            // VABS     I, F    F, D    Absolute
            check(arm32 ? "vabs.f32" : "fabs", 2 * w, abs(f32_1));
            check(arm32 ? "vabs.s32" : "abs", 2 * w, abs(i32_1));
            check(arm32 ? "vabs.s16" : "abs", 4 * w, abs(i16_1));
            check(arm32 ? "vabs.s8" : "abs", 8 * w, abs(i8_1));

            // VACGE    F       -       Absolute Compare Greater Than or Equal
            // VACGT    F       -       Absolute Compare Greater Than
            // VACLE    F       -       Absolute Compare Less Than or Equal
            // VACLT    F       -       Absolute Compare Less Than

            // VADD     I, F    F, D    Add
            check(arm32 ? "vadd.i8" : "add", 8 * w, i8_1 + i8_2);
            check(arm32 ? "vadd.i8" : "add", 8 * w, u8_1 + u8_2);
            check(arm32 ? "vadd.i16" : "add", 4 * w, i16_1 + i16_2);
            check(arm32 ? "vadd.i16" : "add", 4 * w, u16_1 + u16_2);
            check(arm32 ? "vadd.i32" : "add", 2 * w, i32_1 + i32_2);
            check(arm32 ? "vadd.i32" : "add", 2 * w, u32_1 + u32_2);
            check(arm32 ? "vadd.f32" : "fadd", 2 * w, f32_1 + f32_2);
            check(arm32 ? "vadd.i64" : "add", 2 * w, i64_1 + i64_2);
            check(arm32 ? "vadd.i64" : "add", 2 * w, u64_1 + u64_2);

            // VADDHN   I       -       Add and Narrow Returning High Half
            check(arm32 ? "vaddhn.i16" : "addhn", 8 * w, i8((i16_1 + i16_2) / 256));
            check(arm32 ? "vaddhn.i16" : "addhn", 8 * w, u8((u16_1 + u16_2) / 256));
            check(arm32 ? "vaddhn.i32" : "addhn", 4 * w, i16((i32_1 + i32_2) / 65536));
            check(arm32 ? "vaddhn.i32" : "addhn", 4 * w, u16((u32_1 + u32_2) / 65536));
            check(arm32 ? "vaddhn.i64" : "addhn", 4 * w, i32((i64_1 + i64_2) >> 32));
            check(arm32 ? "vaddhn.i64" : "addhn", 4 * w, u32((u64_1 + u64_2) >> 32));

            // VADDL    I       -       Add Long
            check(arm32 ? "vaddl.s8" : "saddl", 8 * w, i16(i8_1) + i16(i8_2));
            check(arm32 ? "vaddl.u8" : "uaddl", 8 * w, u16(u8_1) + u16(u8_2));
            check(arm32 ? "vaddl.s16" : "saddl", 4 * w, i32(i16_1) + i32(i16_2));
            check(arm32 ? "vaddl.u16" : "uaddl", 4 * w, u32(u16_1) + u32(u16_2));
            check(arm32 ? "vaddl.s32" : "saddl", 2 * w, i64(i32_1) + i64(i32_2));
            check(arm32 ? "vaddl.u32" : "uaddl", 2 * w, u64(u32_1) + u64(u32_2));

            // VADDW    I       -       Add Wide
            check(arm32 ? "vaddw.s8" : "saddw", 8 * w, i8_1 + i16_1);
            check(arm32 ? "vaddw.u8" : "uaddw", 8 * w, u8_1 + u16_1);
            check(arm32 ? "vaddw.s16" : "saddw", 4 * w, i16_1 + i32_1);
            check(arm32 ? "vaddw.u16" : "uaddw", 4 * w, u16_1 + u32_1);
            check(arm32 ? "vaddw.s32" : "saddw", 2 * w, i32_1 + i64_1);
            check(arm32 ? "vaddw.u32" : "uaddw", 2 * w, u32_1 + u64_1);

            // VAND     X       -       Bitwise AND
            // Not implemented in front-end yet
            // check("vand", 4, bool1 & bool2);
            // check("vand", 2, bool1 & bool2);

            // VBIC     I       -       Bitwise Clear
            // VBIF     X       -       Bitwise Insert if False
            // VBIT     X       -       Bitwise Insert if True
            // skip these ones

            // VBSL     X       -       Bitwise Select
            check(arm32 ? "vbsl" : "bsl", 2 * w, select(f32_1 > f32_2, 1.0f, 2.0f));

            // VCEQ     I, F    -       Compare Equal
            check(arm32 ? "vceq.i8" : "cmeq", 8 * w, select(i8_1 == i8_2, i8(1), i8(2)));
            check(arm32 ? "vceq.i8" : "cmeq", 8 * w, select(u8_1 == u8_2, u8(1), u8(2)));
            check(arm32 ? "vceq.i16" : "cmeq", 4 * w, select(i16_1 == i16_2, i16(1), i16(2)));
            check(arm32 ? "vceq.i16" : "cmeq", 4 * w, select(u16_1 == u16_2, u16(1), u16(2)));
            check(arm32 ? "vceq.i32" : "cmeq", 2 * w, select(i32_1 == i32_2, i32(1), i32(2)));
            check(arm32 ? "vceq.i32" : "cmeq", 2 * w, select(u32_1 == u32_2, u32(1), u32(2)));
            check(arm32 ? "vceq.f32" : "fcmeq", 2 * w, select(f32_1 == f32_2, 1.0f, 2.0f));

            // VCGE     I, F    -       Compare Greater Than or Equal
#if 0
            // Halide flips these to less than instead
            check("vcge.s8", 16, select(i8_1 >= i8_2, i8(1), i8(2)));
            check("vcge.u8", 16, select(u8_1 >= u8_2, u8(1), u8(2)));
            check("vcge.s16", 8, select(i16_1 >= i16_2, i16(1), i16(2)));
            check("vcge.u16", 8, select(u16_1 >= u16_2, u16(1), u16(2)));
            check("vcge.s32", 4, select(i32_1 >= i32_2, i32(1), i32(2)));
            check("vcge.u32", 4, select(u32_1 >= u32_2, u32(1), u32(2)));
            check("vcge.f32", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));
            check("vcge.s8", 8, select(i8_1 >= i8_2, i8(1), i8(2)));
            check("vcge.u8", 8, select(u8_1 >= u8_2, u8(1), u8(2)));
            check("vcge.s16", 4, select(i16_1 >= i16_2, i16(1), i16(2)));
            check("vcge.u16", 4, select(u16_1 >= u16_2, u16(1), u16(2)));
            check("vcge.s32", 2, select(i32_1 >= i32_2, i32(1), i32(2)));
            check("vcge.u32", 2, select(u32_1 >= u32_2, u32(1), u32(2)));
            check("vcge.f32", 2, select(f32_1 >= f32_2, 1.0f, 2.0f));
#endif

            // VCGT     I, F    -       Compare Greater Than
            check(arm32 ? "vcgt.s8" : "cmgt", 8 * w, select(i8_1 > i8_2, i8(1), i8(2)));
            check(arm32 ? "vcgt.u8" : "cmhi", 8 * w, select(u8_1 > u8_2, u8(1), u8(2)));
            check(arm32 ? "vcgt.s16" : "cmgt", 4 * w, select(i16_1 > i16_2, i16(1), i16(2)));
            check(arm32 ? "vcgt.u16" : "cmhi", 4 * w, select(u16_1 > u16_2, u16(1), u16(2)));
            check(arm32 ? "vcgt.s32" : "cmgt", 2 * w, select(i32_1 > i32_2, i32(1), i32(2)));
            check(arm32 ? "vcgt.u32" : "cmhi", 2 * w, select(u32_1 > u32_2, u32(1), u32(2)));
            check(arm32 ? "vcgt.f32" : "fcmgt", 2 * w, select(f32_1 > f32_2, 1.0f, 2.0f));

            // VCLS     I       -       Count Leading Sign Bits
#if 0
            // We don't currently match these, but it wouldn't be hard to do.
            check(arm32 ? "vcls.s8" : "cls", 8 * w, max(count_leading_zeros(i8_1), count_leading_zeros(~i8_1)));
            check(arm32 ? "vcls.s16" : "cls", 8 * w, max(count_leading_zeros(i16_1), count_leading_zeros(~i16_1)));
            check(arm32 ? "vcls.s32" : "cls", 8 * w, max(count_leading_zeros(i32_1), count_leading_zeros(~i32_1)));
#endif

            // VCLZ     I       -       Count Leading Zeros
            check(arm32 ? "vclz.i8" : "clz", 8 * w, count_leading_zeros(i8_1));
            check(arm32 ? "vclz.i8" : "clz", 8 * w, count_leading_zeros(u8_1));
            check(arm32 ? "vclz.i16" : "clz", 8 * w, count_leading_zeros(i16_1));
            check(arm32 ? "vclz.i16" : "clz", 8 * w, count_leading_zeros(u16_1));
            check(arm32 ? "vclz.i32" : "clz", 8 * w, count_leading_zeros(i32_1));
            check(arm32 ? "vclz.i32" : "clz", 8 * w, count_leading_zeros(u32_1));

            // VCMP     -       F, D    Compare Setting Flags
            // We skip this

            // VCNT     I       -       Count Number of Set Bits
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(i8_1));
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(u8_1));
            // There is only cnt for bytes, and then horizontal adds.
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(i16_1));
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(u16_1));
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(i32_1));
            check(arm32 ? "vcnt.8" : "cnt", 8 * w, popcount(u32_1));

            // VCVT     I, F, H I, F, D, H      Convert Between Floating-Point and 32-bit Integer Types
            check(arm32 ? "vcvt.f32.u32" : "ucvtf", 2 * w, f32(u32_1));
            check(arm32 ? "vcvt.f32.s32" : "scvtf", 2 * w, f32(i32_1));
            check(arm32 ? "vcvt.u32.f32" : "fcvtzu", 2 * w, u32(f32_1));
            check(arm32 ? "vcvt.s32.f32" : "fcvtzs", 2 * w, i32(f32_1));
            // skip the fixed point conversions for now

            // VDIV     -       F, D    Divide
            // This doesn't actually get vectorized in 32-bit. Not sure cortex processors can do vectorized division.
            check(arm32 ? "vdiv.f32" : "fdiv", 2 * w, f32_1 / f32_2);
            check(arm32 ? "vdiv.f64" : "fdiv", 2 * w, f64_1 / f64_2);

            // VDUP     X       -       Duplicate
            check(arm32 ? "vdup.8" : "dup", 16 * w, i8(y));
            check(arm32 ? "vdup.8" : "dup", 16 * w, u8(y));
            check(arm32 ? "vdup.16" : "dup", 8 * w, i16(y));
            check(arm32 ? "vdup.16" : "dup", 8 * w, u16(y));
            check(arm32 ? "vdup.32" : "dup", 4 * w, i32(y));
            check(arm32 ? "vdup.32" : "dup", 4 * w, u32(y));
            check(arm32 ? "vdup.32" : "dup", 4 * w, f32(y));

            // VEOR     X       -       Bitwise Exclusive OR
            // check("veor", 4, bool1 ^ bool2);

            // VEXT     I       -       Extract Elements and Concatenate
            // unaligned loads with known offsets should use vext
#if 0
            // We currently don't do this.
            check("vext.8", 16, in_i8(x+1));
            check("vext.16", 8, in_i16(x+1));
            check("vext.32", 4, in_i32(x+1));
#endif

            // VHADD    I       -       Halving Add
            check(arm32 ? "vhadd.s8" : "shadd", 8 * w, i8((i16(i8_1) + i16(i8_2)) / 2));
            check(arm32 ? "vhadd.u8" : "uhadd", 8 * w, u8((u16(u8_1) + u16(u8_2)) / 2));
            check(arm32 ? "vhadd.s16" : "shadd", 4 * w, i16((i32(i16_1) + i32(i16_2)) / 2));
            check(arm32 ? "vhadd.u16" : "uhadd", 4 * w, u16((u32(u16_1) + u32(u16_2)) / 2));
            check(arm32 ? "vhadd.s32" : "shadd", 2 * w, i32((i64(i32_1) + i64(i32_2)) / 2));
            check(arm32 ? "vhadd.u32" : "uhadd", 2 * w, u32((u64(u32_1) + u64(u32_2)) / 2));

            // Halide doesn't define overflow behavior for i32 so we
            // can use vhadd instruction. We can't use it for unsigned u8,i16,u16,u32.
            check(arm32 ? "vhadd.s32" : "shadd", 2 * w, (i32_1 + i32_2) / 2);

            // VHSUB    I       -       Halving Subtract
            check(arm32 ? "vhsub.s8" : "shsub", 8 * w, i8((i16(i8_1) - i16(i8_2)) / 2));
            check(arm32 ? "vhsub.u8" : "uhsub", 8 * w, u8((u16(u8_1) - u16(u8_2)) / 2));
            check(arm32 ? "vhsub.s16" : "shsub", 4 * w, i16((i32(i16_1) - i32(i16_2)) / 2));
            check(arm32 ? "vhsub.u16" : "uhsub", 4 * w, u16((u32(u16_1) - u32(u16_2)) / 2));
            check(arm32 ? "vhsub.s32" : "shsub", 2 * w, i32((i64(i32_1) - i64(i32_2)) / 2));
            check(arm32 ? "vhsub.u32" : "uhsub", 2 * w, u32((u64(u32_1) - u64(u32_2)) / 2));

            check(arm32 ? "vhsub.s32" : "shsub", 2 * w, (i32_1 - i32_2) / 2);

            // VLD1     X       -       Load Single-Element Structures
            // dense loads with unknown alignments should use vld1 variants
            check(arm32 ? "vld1.8" : "ldr", 8 * w, in_i8(x + y));
            check(arm32 ? "vld1.8" : "ldr", 8 * w, in_u8(x + y));
            check(arm32 ? "vld1.16" : "ldr", 4 * w, in_i16(x + y));
            check(arm32 ? "vld1.16" : "ldr", 4 * w, in_u16(x + y));
            if (w > 1) {
                // When w == 1, llvm emits vldr instead
                check(arm32 ? "vld1.32" : "ldr", 2 * w, in_i32(x + y));
                check(arm32 ? "vld1.32" : "ldr", 2 * w, in_u32(x + y));
                check(arm32 ? "vld1.32" : "ldr", 2 * w, in_f32(x + y));
            }

            if (target.os != Target::IOS && target.os != Target::OSX) {
                // VLD* are not profitable on Apple silicon

                // VLD2     X       -       Load Two-Element Structures
                // These need to be vectorized at least 2 native vectors wide,
                // so we get a full vectors' worth that we know is safe to
                // access.
                check(arm32 ? "vld2.8" : "ld2", 32 * w, in_i8(x * 2));
                check(arm32 ? "vld2.8" : "ld2", 32 * w, in_u8(x * 2));
                check(arm32 ? "vld2.16" : "ld2", 16 * w, in_i16(x * 2));
                check(arm32 ? "vld2.16" : "ld2", 16 * w, in_u16(x * 2));
                check(arm32 ? "vld2.32" : "ld2", 8 * w, in_i32(x * 2));
                check(arm32 ? "vld2.32" : "ld2", 8 * w, in_u32(x * 2));
                check(arm32 ? "vld2.32" : "ld2", 8 * w, in_f32(x * 2));

                // VLD3     X       -       Load Three-Element Structures
                check(arm32 ? "vld3.8" : "ld3", 32 * w, in_i8(x * 3));
                check(arm32 ? "vld3.8" : "ld3", 32 * w, in_u8(x * 3));
                check(arm32 ? "vld3.16" : "ld3", 16 * w, in_i16(x * 3));
                check(arm32 ? "vld3.16" : "ld3", 16 * w, in_u16(x * 3));
                check(arm32 ? "vld3.32" : "ld3", 8 * w, in_i32(x * 3));
                check(arm32 ? "vld3.32" : "ld3", 8 * w, in_u32(x * 3));
                check(arm32 ? "vld3.32" : "ld3", 8 * w, in_f32(x * 3));

                // VLD4     X       -       Load Four-Element Structures
                check(arm32 ? "vld4.8" : "ld4", 32 * w, in_i8(x * 4));
                check(arm32 ? "vld4.8" : "ld4", 32 * w, in_u8(x * 4));
                check(arm32 ? "vld4.16" : "ld4", 16 * w, in_i16(x * 4));
                check(arm32 ? "vld4.16" : "ld4", 16 * w, in_u16(x * 4));
                check(arm32 ? "vld4.32" : "ld4", 8 * w, in_i32(x * 4));
                check(arm32 ? "vld4.32" : "ld4", 8 * w, in_u32(x * 4));
                check(arm32 ? "vld4.32" : "ld4", 8 * w, in_f32(x * 4));
            } else if (!arm32) {
                // On Apple Silicon we expect dense loads followed by shuffles.
                check("uzp1.16b", 32 * w, in_i8(x * 2));
                check("uzp1.16b", 32 * w, in_u8(x * 2));
                check("uzp1.8h", 16 * w, in_i16(x * 2));
                check("uzp1.8h", 16 * w, in_u16(x * 2));
                check("uzp1.4s", 8 * w, in_i32(x * 2));
                check("uzp1.4s", 8 * w, in_u32(x * 2));
                check("uzp1.4s", 8 * w, in_f32(x * 2));

                // VLD3     X       -       Load Three-Element Structures
                check("tbl.16b", 32 * w, in_i8(x * 3));
                check("tbl.16b", 32 * w, in_u8(x * 3));
                check("tbl.16b", 16 * w, in_i16(x * 3));
                check("tbl.16b", 16 * w, in_u16(x * 3));
                // For 32-bit types llvm just scalarizes

                // VLD4     X       -       Load Four-Element Structures
                check("tbl.16b", 32 * w, in_i8(x * 4));
                check("tbl.16b", 32 * w, in_u8(x * 4));
                check("tbl.16b", 16 * w, in_i16(x * 4));
                check("tbl.16b", 16 * w, in_u16(x * 4));
            }

            // VLDM     X       F, D    Load Multiple Registers
            // VLDR     X       F, D    Load Single Register
            // We generally generate vld instead

            // VMAX     I, F    -       Maximum
            check(arm32 ? "vmax.s8" : "smax", 8 * w, max(i8_1, i8_2));
            check(arm32 ? "vmax.u8" : "umax", 8 * w, max(u8_1, u8_2));
            check(arm32 ? "vmax.s16" : "smax", 4 * w, max(i16_1, i16_2));
            check(arm32 ? "vmax.u16" : "umax", 4 * w, max(u16_1, u16_2));
            check(arm32 ? "vmax.s32" : "smax", 2 * w, max(i32_1, i32_2));
            check(arm32 ? "vmax.u32" : "umax", 2 * w, max(u32_1, u32_2));
            check(arm32 ? "vmax.f32" : "fmax", 2 * w, max(f32_1, f32_2));

            // VMIN     I, F    -       Minimum
            check(arm32 ? "vmin.s8" : "smin", 8 * w, min(i8_1, i8_2));
            check(arm32 ? "vmin.u8" : "umin", 8 * w, min(u8_1, u8_2));
            check(arm32 ? "vmin.s16" : "smin", 4 * w, min(i16_1, i16_2));
            check(arm32 ? "vmin.u16" : "umin", 4 * w, min(u16_1, u16_2));
            check(arm32 ? "vmin.s32" : "smin", 2 * w, min(i32_1, i32_2));
            check(arm32 ? "vmin.u32" : "umin", 2 * w, min(u32_1, u32_2));
            check(arm32 ? "vmin.f32" : "fmin", 2 * w, min(f32_1, f32_2));

            // VMLA     I, F    F, D    Multiply Accumulate
            check(arm32 ? "vmla.i8" : "mla", 8 * w, i8_1 + i8_2 * i8_3);
            check(arm32 ? "vmla.i8" : "mla", 8 * w, u8_1 + u8_2 * u8_3);
            check(arm32 ? "vmla.i16" : "mla", 4 * w, i16_1 + i16_2 * i16_3);
            check(arm32 ? "vmla.i16" : "mla", 4 * w, u16_1 + u16_2 * u16_3);
            check(arm32 ? "vmla.i32" : "mla", 2 * w, i32_1 + i32_2 * i32_3);
            check(arm32 ? "vmla.i32" : "mla", 2 * w, u32_1 + u32_2 * u32_3);
            if (w == 1 || w == 2) {
                // Older llvms don't always fuse this at non-native widths
                // TODO: Re-enable this after fixing https://github.com/halide/Halide/issues/3477
                // check(arm32 ? "vmla.f32" : "fmla", 2*w, f32_1 + f32_2*f32_3);
                if (!arm32)
                    check(arm32 ? "vmla.f32" : "fmla", 2 * w, f32_1 + f32_2 * f32_3);
            }

            // VMLS     I, F    F, D    Multiply Subtract
            check(arm32 ? "vmls.i8" : "mls", 8 * w, i8_1 - i8_2 * i8_3);
            check(arm32 ? "vmls.i8" : "mls", 8 * w, u8_1 - u8_2 * u8_3);
            check(arm32 ? "vmls.i16" : "mls", 4 * w, i16_1 - i16_2 * i16_3);
            check(arm32 ? "vmls.i16" : "mls", 4 * w, u16_1 - u16_2 * u16_3);
            check(arm32 ? "vmls.i32" : "mls", 2 * w, i32_1 - i32_2 * i32_3);
            check(arm32 ? "vmls.i32" : "mls", 2 * w, u32_1 - u32_2 * u32_3);
            if (w == 1 || w == 2) {
                // Older llvms don't always fuse this at non-native widths
                // TODO: Re-enable this after fixing https://github.com/halide/Halide/issues/3477
                // check(arm32 ? "vmls.f32" : "fmls", 2*w, f32_1 - f32_2*f32_3);
                if (!arm32)
                    check(arm32 ? "vmls.f32" : "fmls", 2 * w, f32_1 - f32_2 * f32_3);
            }

            // VMLAL    I       -       Multiply Accumulate Long
            check(arm32 ? "vmlal.s8" : "smlal", 8 * w, i16_1 + i16(i8_2) * i8_3);
            check(arm32 ? "vmlal.s8" : "smlal", 8 * w, i16_1 + i16(i8_2) * 2);
            check(arm32 ? "vmlal.u8" : "umlal", 8 * w, u16_1 + u16(u8_2) * u8_3);
            check(arm32 ? "vmlal.u8" : "umlal", 8 * w, u16_1 + u16(u8_2) * 2);
            check(arm32 ? "vmlal.s16" : "smlal", 4 * w, i32_1 + i32(i16_2) * i16_3);
            check(arm32 ? "vmlal.s16" : "smlal", 4 * w, i32_1 + i32(i16_2) * 2);
            check(arm32 ? "vmlal.u16" : "umlal", 4 * w, u32_1 + u32(u16_2) * u16_3);
            check(arm32 ? "vmlal.u16" : "umlal", 4 * w, u32_1 + u32(u16_2) * 2);
            check(arm32 ? "vmlal.s32" : "smlal", 2 * w, i64_1 + i64(i32_2) * i32_3);
            check(arm32 ? "vmlal.s32" : "smlal", 2 * w, i64_1 + i64(i32_2) * 2);
            check(arm32 ? "vmlal.u32" : "umlal", 2 * w, u64_1 + u64(u32_2) * u32_3);
            check(arm32 ? "vmlal.u32" : "umlal", 2 * w, u64_1 + u64(u32_2) * 2);

            // VMLSL    I       -       Multiply Subtract Long
            check(arm32 ? "vmlsl.s8" : "smlsl", 8 * w, i16_1 - i16(i8_2) * i8_3);
            check(arm32 ? "vmlsl.s8" : "smlsl", 8 * w, i16_1 - i16(i8_2) * 2);
            check(arm32 ? "vmlsl.u8" : "umlsl", 8 * w, u16_1 - u16(u8_2) * u8_3);
            check(arm32 ? "vmlsl.u8" : "umlsl", 8 * w, u16_1 - u16(u8_2) * 2);
            check(arm32 ? "vmlsl.s16" : "smlsl", 4 * w, i32_1 - i32(i16_2) * i16_3);
            check(arm32 ? "vmlsl.s16" : "smlsl", 4 * w, i32_1 - i32(i16_2) * 2);
            check(arm32 ? "vmlsl.u16" : "umlsl", 4 * w, u32_1 - u32(u16_2) * u16_3);
            check(arm32 ? "vmlsl.u16" : "umlsl", 4 * w, u32_1 - u32(u16_2) * 2);
            check(arm32 ? "vmlsl.s32" : "smlsl", 2 * w, i64_1 - i64(i32_2) * i32_3);
            check(arm32 ? "vmlsl.s32" : "smlsl", 2 * w, i64_1 - i64(i32_2) * 2);
            check(arm32 ? "vmlsl.u32" : "umlsl", 2 * w, u64_1 - u64(u32_2) * u32_3);
            check(arm32 ? "vmlsl.u32" : "umlsl", 2 * w, u64_1 - u64(u32_2) * 2);

            // VMOV     X       F, D    Move Register or Immediate
            // This is for loading immediates, which we won't do in the inner loop anyway

            // VMOVL    I       -       Move Long
            // For aarch64, llvm does a widening shift by 0 instead of using the sxtl instruction.
            check(arm32 ? "vmovl.s8" : "sshll", 8 * w, i16(i8_1));
            check(arm32 ? "vmovl.u8" : "ushll", 8 * w, u16(u8_1));
            check(arm32 ? "vmovl.u8" : "ushll", 8 * w, i16(u8_1));
            check(arm32 ? "vmovl.s16" : "sshll", 4 * w, i32(i16_1));
            check(arm32 ? "vmovl.u16" : "ushll", 4 * w, u32(u16_1));
            check(arm32 ? "vmovl.u16" : "ushll", 4 * w, i32(u16_1));
            check(arm32 ? "vmovl.s32" : "sshll", 2 * w, i64(i32_1));
            check(arm32 ? "vmovl.u32" : "ushll", 2 * w, u64(u32_1));
            check(arm32 ? "vmovl.u32" : "ushll", 2 * w, i64(u32_1));

            // VMOVN    I       -       Move and Narrow
            if (w > 1) {
                check(arm32 ? "vmovn.i16" : "uzp1", 8 * w, i8(i16_1));
                check(arm32 ? "vmovn.i16" : "uzp1", 8 * w, u8(u16_1));
                check(arm32 ? "vmovn.i32" : "uzp1", 4 * w, i16(i32_1));
                check(arm32 ? "vmovn.i32" : "uzp1", 4 * w, u16(u32_1));
                check(arm32 ? "vmovn.i64" : "uzp1", 2 * w, i32(i64_1));
                check(arm32 ? "vmovn.i64" : "uzp1", 2 * w, u32(u64_1));
            } else {
                check(arm32 ? "vmovn.i16" : "xtn", 8 * w, i8(i16_1));
                check(arm32 ? "vmovn.i16" : "xtn", 8 * w, u8(u16_1));
                check(arm32 ? "vmovn.i32" : "xtn", 4 * w, i16(i32_1));
                check(arm32 ? "vmovn.i32" : "xtn", 4 * w, u16(u32_1));
                check(arm32 ? "vmovn.i64" : "xtn", 2 * w, i32(i64_1));
                check(arm32 ? "vmovn.i64" : "xtn", 2 * w, u32(u64_1));
            }

            // VMRS     X       F, D    Move Advanced SIMD or VFP Register to ARM compute Engine
            // VMSR     X       F, D    Move ARM Core Register to Advanced SIMD or VFP
            // trust llvm to use this correctly

            // VMUL     I, F, P F, D    Multiply
            check(arm32 ? "vmul.f64" : "fmul", 2 * w, f64_2 * f64_1);
            check(arm32 ? "vmul.i8" : "mul", 8 * w, i8_2 * i8_1);
            check(arm32 ? "vmul.i8" : "mul", 8 * w, u8_2 * u8_1);
            check(arm32 ? "vmul.i16" : "mul", 4 * w, i16_2 * i16_1);
            check(arm32 ? "vmul.i16" : "mul", 4 * w, u16_2 * u16_1);
            check(arm32 ? "vmul.i32" : "mul", 2 * w, i32_2 * i32_1);
            check(arm32 ? "vmul.i32" : "mul", 2 * w, u32_2 * u32_1);
            check(arm32 ? "vmul.f32" : "fmul", 2 * w, f32_2 * f32_1);

            // VMULL    I, F, P -       Multiply Long
            check(arm32 ? "vmull.s8" : "smull", 8 * w, i16(i8_1) * i8_2);
            check(arm32 ? "vmull.u8" : "umull", 8 * w, u16(u8_1) * u8_2);
            check(arm32 ? "vmull.s16" : "smull", 4 * w, i32(i16_1) * i16_2);
            check(arm32 ? "vmull.u16" : "umull", 4 * w, u32(u16_1) * u16_2);
            check(arm32 ? "vmull.s32" : "smull", 2 * w, i64(i32_1) * i32_2);
            check(arm32 ? "vmull.u32" : "umull", 2 * w, u64(u32_1) * u32_2);

            // integer division by a constant should use fixed point unsigned
            // multiplication, which is done by using a widening multiply
            // followed by a narrowing
            check(arm32 ? "vmull.u8" : "umull", 8 * w, i8_1 / 37);
            check(arm32 ? "vmull.u8" : "umull", 8 * w, u8_1 / 37);
            check(arm32 ? "vmull.u16" : "umull", 4 * w, i16_1 / 37);
            check(arm32 ? "vmull.u16" : "umull", 4 * w, u16_1 / 37);
            check(arm32 ? "vmull.u32" : "umull", 2 * w, i32_1 / 37);
            check(arm32 ? "vmull.u32" : "umull", 2 * w, u32_1 / 37);

            // VMVN     X       -       Bitwise NOT
            // check("vmvn", ~bool1);

            // VNEG     I, F    F, D    Negate
            check(arm32 ? "vneg.s8" : "neg", 8 * w, -i8_1);
            check(arm32 ? "vneg.s16" : "neg", 4 * w, -i16_1);
            check(arm32 ? "vneg.s32" : "neg", 2 * w, -i32_1);
            check(arm32 ? "vneg.f32" : "fneg", 4 * w, -f32_1);
            check(arm32 ? "vneg.f64" : "fneg", 2 * w, -f64_1);

            // VNMLA    -       F, D    Negative Multiply Accumulate
            // VNMLS    -       F, D    Negative Multiply Subtract
            // VNMUL    -       F, D    Negative Multiply
#if 0
            // These are vfp, not neon. They only work on scalars
            check("vnmla.f32", 4, -(f32_1 + f32_2*f32_3));
            check("vnmla.f64", 2, -(f64_1 + f64_2*f64_3));
            check("vnmls.f32", 4, -(f32_1 - f32_2*f32_3));
            check("vnmls.f64", 2, -(f64_1 - f64_2*f64_3));
            check("vnmul.f32", 4, -(f32_1*f32_2));
            check("vnmul.f64", 2, -(f64_1*f64_2));
#endif

            // VORN     X       -       Bitwise OR NOT
            // check("vorn", bool1 | (~bool2));

            // VORR     X       -       Bitwise OR
            // check("vorr", bool1 | bool2);

            {
                for (int f : {2, 4}) {
                    RDom r(0, f);

                    // A summation reduction that starts at something
                    // non-trivial, to avoid llvm simplifying accumulating
                    // widening summations into just widening summations.
                    auto sum_ = [&](Expr e) {
                        Func f;
                        f(x) = cast(e.type(), 123);
                        f(x) += e;
                        return f(x);
                    };

                    // VPADD    I, F    -       Pairwise Add
                    check(arm32 ? "vpadd.i8" : "addp", 16, sum_(in_i8(f * x + r)));
                    check(arm32 ? "vpadd.i8" : "addp", 16, sum_(in_u8(f * x + r)));
                    check(arm32 ? "vpadd.i16" : "addp", 8, sum_(in_i16(f * x + r)));
                    check(arm32 ? "vpadd.i16" : "addp", 8, sum_(in_u16(f * x + r)));
                    check(arm32 ? "vpadd.i32" : "addp", 4, sum_(in_i32(f * x + r)));
                    check(arm32 ? "vpadd.i32" : "addp", 4, sum_(in_u32(f * x + r)));
                    check(arm32 ? "vpadd.f32" : "faddp", 4, sum_(in_f32(f * x + r)));
                    // In 32-bit, we don't have a pairwise op for doubles,
                    // and expect to just get vadd instructions on d
                    // registers.
                    check(arm32 ? "vadd.f64" : "faddp", 4, sum_(in_f64(f * x + r)));

                    if (f == 2) {
                        // VPADAL   I       -       Pairwise Add and Accumulate Long

                        // If we're reducing by a factor of two, we can
                        // use the forms with an accumulator
                        check(arm32 ? "vpadal.s8" : "sadalp", 16, sum_(i16(in_i8(f * x + r))));
                        check(arm32 ? "vpadal.u8" : "uadalp", 16, sum_(i16(in_u8(f * x + r))));
                        check(arm32 ? "vpadal.u8" : "uadalp*", 16, sum_(u16(in_u8(f * x + r))));

                        check(arm32 ? "vpadal.s16" : "sadalp", 8, sum_(i32(in_i16(f * x + r))));
                        check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(i32(in_u16(f * x + r))));
                        check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(u32(in_u16(f * x + r))));

                        check(arm32 ? "vpadal.s32" : "sadalp", 4, sum_(i64(in_i32(f * x + r))));
                        check(arm32 ? "vpadal.u32" : "uadalp", 4, sum_(i64(in_u32(f * x + r))));
                        check(arm32 ? "vpadal.u32" : "uadalp", 4, sum_(u64(in_u32(f * x + r))));
                    } else {
                        // VPADDL   I       -       Pairwise Add Long

                        // If we're reducing by more than that, that's not
                        // possible.
                        check(arm32 ? "vpaddl.s8" : "saddlp", 16, sum_(i16(in_i8(f * x + r))));
                        check(arm32 ? "vpaddl.u8" : "uaddlp", 16, sum_(i16(in_u8(f * x + r))));
                        check(arm32 ? "vpaddl.u8" : "uaddlp", 16, sum_(u16(in_u8(f * x + r))));

                        check(arm32 ? "vpaddl.s16" : "saddlp", 8, sum_(i32(in_i16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 8, sum_(i32(in_u16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 8, sum_(u32(in_u16(f * x + r))));

                        check(arm32 ? "vpaddl.s32" : "saddlp", 4, sum_(i64(in_i32(f * x + r))));
                        check(arm32 ? "vpaddl.u32" : "uaddlp", 4, sum_(i64(in_u32(f * x + r))));
                        check(arm32 ? "vpaddl.u32" : "uaddlp", 4, sum_(u64(in_u32(f * x + r))));

                        // If we're widening the type by a factor of four
                        // as well as reducing by a factor of four, we
                        // expect vpaddl followed by vpadal
                        if (target.has_feature(Target::ARMDotProd)) {
                            check(arm32 ? "vpaddl.s8" : "sdot", 8, sum_(i32(in_i8(f * x + r))));
                            check(arm32 ? "vpaddl.u8" : "udot", 8, sum_(i32(in_u8(f * x + r))));
                            check(arm32 ? "vpaddl.u8" : "udot", 8, sum_(u32(in_u8(f * x + r))));
                            if (!arm32) {
                                check("sdot", 8, i32_1 + i32(i8_1) * 3 + i32(i8_2) * 6 + i32(i8_3) * 9 + i32(i8_4) * 12);
                                check("sdot", 8, i32_1 + i32(i8_1) * 3 + i32(i8_2) * 6 + i32(i8_3) * 9 + i32(i8_4));
                                check("sdot", 8, i32_1 + i32(i8_1) * 3 + i32(i8_2) * 6 + i32(i8_3) + i32(i8_4) * 12);
                                check("sdot", 8, i32_1 + i32(i8_1) * 3 + i32(i8_2) + i32(i8_3) * 9 + i32(i8_4) * 12);
                                check("sdot", 8, i32_1 + i32(i8_1) + i32(i8_2) * 6 + i32(i8_3) * 9 + i32(i8_4) * 12);

                                check("udot", 8, u32_1 + u32(u8_1) * 3 + u32(u8_2) * 6 + u32(u8_3) * 9 + u32(u8_4) * 12);
                                check("udot", 8, u32_1 + u32(u8_1) * 3 + u32(u8_2) * 6 + u32(u8_3) * 9 + u32(u8_4));
                                check("udot", 8, u32_1 + u32(u8_1) * 3 + u32(u8_2) * 6 + u32(u8_3) + u32(u8_4) * 12);
                                check("udot", 8, u32_1 + u32(u8_1) * 3 + u32(u8_2) + u32(u8_3) * 9 + u32(u8_4) * 12);
                                check("udot", 8, u32_1 + u32(u8_1) + u32(u8_2) * 6 + u32(u8_3) * 9 + u32(u8_4) * 12);
                            }
                        } else {
                            check(arm32 ? "vpaddl.s8" : "saddlp", 8, sum_(i32(in_i8(f * x + r))));
                            check(arm32 ? "vpaddl.u8" : "uaddlp", 8, sum_(i32(in_u8(f * x + r))));
                            check(arm32 ? "vpaddl.u8" : "uaddlp", 8, sum_(u32(in_u8(f * x + r))));
                        }
                        check(arm32 ? "vpaddl.s16" : "saddlp", 4, sum_(i64(in_i16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 4, sum_(i64(in_u16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 4, sum_(u64(in_u16(f * x + r))));

                        // Note that when going from u8 to i32 like this,
                        // the vpaddl is unsigned and the vpadal is a
                        // signed, because the intermediate type is u16
                        if (target.has_feature(Target::ARMDotProd)) {
                            check(arm32 ? "vpadal.s16" : "sdot", 8, sum_(i32(in_i8(f * x + r))));
                            check(arm32 ? "vpadal.u16" : "udot", 8, sum_(i32(in_u8(f * x + r))));
                            check(arm32 ? "vpadal.u16" : "udot", 8, sum_(u32(in_u8(f * x + r))));
                        } else {
                            check(arm32 ? "vpadal.s16" : "sadalp", 8, sum_(i32(in_i8(f * x + r))));
                            check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(i32(in_u8(f * x + r))));
                            check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(u32(in_u8(f * x + r))));
                        }
                        check(arm32 ? "vpadal.s32" : "sadalp", 4, sum_(i64(in_i16(f * x + r))));
                        check(arm32 ? "vpadal.u32" : "uadalp", 4, sum_(i64(in_u16(f * x + r))));
                        check(arm32 ? "vpadal.u32" : "uadalp", 4, sum_(u64(in_u16(f * x + r))));
                    }

                    // VPMAX    I, F    -       Pairwise Maximum
                    check(arm32 ? "vpmax.s8" : "smaxp", 16, maximum(in_i8(f * x + r)));
                    check(arm32 ? "vpmax.u8" : "umaxp", 16, maximum(in_u8(f * x + r)));
                    check(arm32 ? "vpmax.s16" : "smaxp", 8, maximum(in_i16(f * x + r)));
                    check(arm32 ? "vpmax.u16" : "umaxp", 8, maximum(in_u16(f * x + r)));
                    check(arm32 ? "vpmax.s32" : "smaxp", 4, maximum(in_i32(f * x + r)));
                    check(arm32 ? "vpmax.u32" : "umaxp", 4, maximum(in_u32(f * x + r)));

                    // VPMIN    I, F    -       Pairwise Minimum
                    check(arm32 ? "vpmin.s8" : "sminp", 16, minimum(in_i8(f * x + r)));
                    check(arm32 ? "vpmin.u8" : "uminp", 16, minimum(in_u8(f * x + r)));
                    check(arm32 ? "vpmin.s16" : "sminp", 8, minimum(in_i16(f * x + r)));
                    check(arm32 ? "vpmin.u16" : "uminp", 8, minimum(in_u16(f * x + r)));
                    check(arm32 ? "vpmin.s32" : "sminp", 4, minimum(in_i32(f * x + r)));
                    check(arm32 ? "vpmin.u32" : "uminp", 4, minimum(in_u32(f * x + r)));
                }

                // UDOT/SDOT
                if (target.has_feature(Target::ARMDotProd)) {
                    for (int f : {4, 8}) {
                        RDom r(0, f);
                        for (int v : {2, 4}) {
                            check("udot", v, sum(u32(in_u8(f * x + r)) * in_u8(f * x + r + 32)));
                            check("sdot", v, sum(i32(in_i8(f * x + r)) * in_i8(f * x + r + 32)));
                            if (f == 4) {
                                // This doesn't generate for higher reduction factors because the
                                // intermediate is 16-bit instead of 32-bit. It seems like it would
                                // be slower to fix this (because the intermediate sum would be
                                // 32-bit instead of 16-bit).
                                check("udot", v, sum(u32(in_u8(f * x + r))));
                                check("sdot", v, sum(i32(in_i8(f * x + r))));
                            }
                        }
                    }
                }
            }
            // VPOP     X       F, D    Pop from Stack
            // VPUSH    X       F, D    Push to Stack
            // Not used by us

            // VQABS    I       -       Saturating Absolute
#if 0
            // Of questionable value. Catching abs calls is annoying, and the
            // slow path is only one more op (for the max).
            check("vqabs.s8", 16, abs(max(i8_1, -max_i8)));
            check("vqabs.s8", 8, abs(max(i8_1, -max_i8)));
            check("vqabs.s16", 8, abs(max(i16_1, -max_i16)));
            check("vqabs.s16", 4, abs(max(i16_1, -max_i16)));
            check("vqabs.s32", 4, abs(max(i32_1, -max_i32)));
            check("vqabs.s32", 2, abs(max(i32_1, -max_i32)));
#endif

            // VQADD    I       -       Saturating Add
            check(arm32 ? "vqadd.s8" : "sqadd", 8 * w, i8_sat(i16(i8_1) + i16(i8_2)));
            check(arm32 ? "vqadd.s16" : "sqadd", 4 * w, i16_sat(i32(i16_1) + i32(i16_2)));
            check(arm32 ? "vqadd.s32" : "sqadd", 2 * w, i32_sat(i64(i32_1) + i64(i32_2)));

            check(arm32 ? "vqadd.u8" : "uqadd", 8 * w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check(arm32 ? "vqadd.u16" : "uqadd", 4 * w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check(arm32 ? "vqadd.u32" : "uqadd", 2 * w, u32(min(u64(u32_1) + u64(u32_2), max_u32)));

            // Check the case where we add a constant that could be narrowed
            check(arm32 ? "vqadd.u8" : "uqadd", 8 * w, u8(min(u16(u8_1) + 17, max_u8)));
            check(arm32 ? "vqadd.u16" : "uqadd", 4 * w, u16(min(u32(u16_1) + 17, max_u16)));
            check(arm32 ? "vqadd.u32" : "uqadd", 2 * w, u32(min(u64(u32_1) + 17, max_u32)));

            // Can't do larger ones because we can't represent the intermediate 128-bit wide ops.

            // VQDMLAL  I       -       Saturating Double Multiply Accumulate Long
            // VQDMLSL  I       -       Saturating Double Multiply Subtract Long
            // We don't do these, but it would be possible.

            // VQDMULH  I       -       Saturating Doubling Multiply Returning High Half
            // VQDMULL  I       -       Saturating Doubling Multiply Long
            check(arm32 ? "vqdmulh.s16" : "sqdmulh", 4 * w, i16_sat((i32(i16_1) * i32(i16_2)) >> 15));
            check(arm32 ? "vqdmulh.s32" : "sqdmulh", 2 * w, i32_sat((i64(i32_1) * i64(i32_2)) >> 31));

            // VQMOVN   I       -       Saturating Move and Narrow
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8 * w, i8_sat(i16_1));
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8 * w, i8_sat(i32_1));
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8 * w, i8_sat(i64_1));
            check(arm32 ? "vqmovn.s32" : "sqxtn", 4 * w, i16_sat(i32_1));
            check(arm32 ? "vqmovn.s32" : "sqxtn", 4 * w, i16_sat(i64_1));
            check(arm32 ? "vqmovn.s64" : "sqxtn", 2 * w, i32_sat(i64_1));
            check(arm32 ? "vqmovn.u16" : "uqxtn", 8 * w, u8(min(u16_1, max_u8)));
            check(arm32 ? "vqmovn.u16" : "uqxtn", 8 * w, u8(min(u32_1, max_u8)));
            check(arm32 ? "vqmovn.u16" : "uqxtn", 8 * w, u8(min(u64_1, max_u8)));
            check(arm32 ? "vqmovn.u32" : "uqxtn", 4 * w, u16(min(u32_1, max_u16)));
            check(arm32 ? "vqmovn.u32" : "uqxtn", 4 * w, u16(min(u64_1, max_u16)));
            check(arm32 ? "vqmovn.u64" : "uqxtn", 2 * w, u32(min(u64_1, max_u32)));
            // Double/Triple saturating narrow from float
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8 * w, i8_sat(f32_1));
            check(arm32 ? "vqmovn.s16" : "sqxtn", 8 * w, i8_sat(f64_1));
            check(arm32 ? "vqmovn.s32" : "sqxtn", 4 * w, i16_sat(f64_1));

            // VQMOVUN  I       -       Saturating Move and Unsigned Narrow
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8 * w, u8_sat(i16_1));
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8 * w, u8_sat(i32_1));
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8 * w, u8_sat(i64_1));
            check(arm32 ? "vqmovun.s32" : "sqxtun", 4 * w, u16_sat(i32_1));
            check(arm32 ? "vqmovun.s32" : "sqxtun", 4 * w, u16_sat(i64_1));
            check(arm32 ? "vqmovun.s64" : "sqxtun", 2 * w, u32_sat(i64_1));
            // Double/Triple saturating narrow from float
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8 * w, u8_sat(f32_1));
            check(arm32 ? "vqmovun.s16" : "sqxtun", 8 * w, u8_sat(f64_1));
            check(arm32 ? "vqmovun.s32" : "sqxtun", 4 * w, u16_sat(f64_1));

            // VQNEG    I       -       Saturating Negate
            check(arm32 ? "vqneg.s8" : "sqneg", 8 * w, -max(i8_1, -max_i8));
            check(arm32 ? "vqneg.s16" : "sqneg", 4 * w, -max(i16_1, -max_i16));
            check(arm32 ? "vqneg.s32" : "sqneg", 2 * w, -max(i32_1, -max_i32));

            // VQRDMULH I       -       Saturating Rounding Doubling Multiply Returning High Half
            // Note: division in Halide always rounds down (not towards
            // zero). Otherwise these patterns would be more complicated.
            check(arm32 ? "vqrdmulh.s16" : "sqrdmulh", 4 * w, i16_sat((i32(i16_1) * i32(i16_2) + (1 << 14)) / (1 << 15)));
            check(arm32 ? "vqrdmulh.s32" : "sqrdmulh", 2 * w, i32_sat((i64(i32_1) * i64(i32_2) + (1 << 30)) / (Expr(int64_t(1)) << 31)));

            // VQRSHRN   I       -       Saturating Rounding Shift Right Narrow
            // VQRSHRUN  I       -       Saturating Rounding Shift Right Unsigned Narrow
            check(arm32 ? "vqrshrn.s16" : "sqrshrn", 8 * w, i8_sat((i32(i16_1) + 8) / 16));
            check(arm32 ? "vqrshrn.s32" : "sqrshrn", 4 * w, i16_sat((i32_1 + 8) / 16));
            check(arm32 ? "vqrshrn.s64" : "sqrshrn", 2 * w, i32_sat((i64_1 + 8) / 16));
            check(arm32 ? "vqrshrun.s16" : "sqrshrun", 8 * w, u8_sat((i32(i16_1) + 8) / 16));
            check(arm32 ? "vqrshrun.s32" : "sqrshrun", 4 * w, u16_sat((i32_1 + 8) / 16));
            check(arm32 ? "vqrshrun.s64" : "sqrshrun", 2 * w, u32_sat((i64_1 + 8) / 16));
            check(arm32 ? "vqrshrn.u16" : "uqrshrn", 8 * w, u8(min((u32(u16_1) + 8) / 16, max_u8)));
            check(arm32 ? "vqrshrn.u32" : "uqrshrn", 4 * w, u16(min((u64(u32_1) + 8) / 16, max_u16)));
            // check(arm32 ? "vqrshrn.u64" : "uqrshrn", 2 * w, u32(min((u64_1 + 8) / 16, max_u32)));

            // VQSHL    I       -       Saturating Shift Left
            check(arm32 ? "vqshl.s8" : "sqshl", 8 * w, i8_sat(i16(i8_1) * 16));
            check(arm32 ? "vqshl.s16" : "sqshl", 4 * w, i16_sat(i32(i16_1) * 16));
            check(arm32 ? "vqshl.s32" : "sqshl", 2 * w, i32_sat(i64(i32_1) * 16));
            check(arm32 ? "vqshl.u8" : "uqshl", 8 * w, u8(min(u16(u8_1) * 16, max_u8)));
            check(arm32 ? "vqshl.u16" : "uqshl", 4 * w, u16(min(u32(u16_1) * 16, max_u16)));
            check(arm32 ? "vqshl.u32" : "uqshl", 2 * w, u32(min(u64(u32_1) * 16, max_u32)));

            // VQSHLU   I       -       Saturating Shift Left Unsigned
            check(arm32 ? "vqshlu.s8" : "sqshlu", 8 * w, u8_sat(i16(i8_1) * 16));
            check(arm32 ? "vqshlu.s16" : "sqshlu", 4 * w, u16_sat(i32(i16_1) * 16));
            check(arm32 ? "vqshlu.s32" : "sqshlu", 2 * w, u32_sat(i64(i32_1) * 16));

            // VQSHRN   I       -       Saturating Shift Right Narrow
            // VQSHRUN  I       -       Saturating Shift Right Unsigned Narrow
            check(arm32 ? "vqshrn.s16" : "sqshrn", 8 * w, i8_sat(i16_1 / 16));
            check(arm32 ? "vqshrn.s32" : "sqshrn", 4 * w, i16_sat(i32_1 / 16));
            check(arm32 ? "vqshrn.s64" : "sqshrn", 2 * w, i32_sat(i64_1 / 16));
            check(arm32 ? "vqshrun.s16" : "sqshrun", 8 * w, u8_sat(i16_1 / 16));
            check(arm32 ? "vqshrun.s32" : "sqshrun", 4 * w, u16_sat(i32_1 / 16));
            check(arm32 ? "vqshrun.s64" : "sqshrun", 2 * w, u32_sat(i64_1 / 16));
            check(arm32 ? "vqshrn.u16" : "uqshrn", 8 * w, u8(min(u16_1 / 16, max_u8)));
            check(arm32 ? "vqshrn.u32" : "uqshrn", 4 * w, u16(min(u32_1 / 16, max_u16)));
            check(arm32 ? "vqshrn.u64" : "uqshrn", 2 * w, u32(min(u64_1 / 16, max_u32)));

            // VQSUB    I       -       Saturating Subtract
            check(arm32 ? "vqsub.s8" : "sqsub", 8 * w, i8_sat(i16(i8_1) - i16(i8_2)));
            check(arm32 ? "vqsub.s16" : "sqsub", 4 * w, i16_sat(i32(i16_1) - i32(i16_2)));
            check(arm32 ? "vqsub.s32" : "sqsub", 2 * w, i32_sat(i64(i32_1) - i64(i32_2)));

            // N.B. Saturating subtracts are expressed by widening to a igned* type
            check(arm32 ? "vqsub.u8" : "uqsub", 8 * w, u8_sat(i16(u8_1) - i16(u8_2)));
            check(arm32 ? "vqsub.u16" : "uqsub", 4 * w, u16_sat(i32(u16_1) - i32(u16_2)));
            check(arm32 ? "vqsub.u32" : "uqsub", 2 * w, u32_sat(i64(u32_1) - i64(u32_2)));

            // VRADDHN  I       -       Rounding Add and Narrow Returning High Half
            check(arm32 ? "vraddhn.i16" : "raddhn", 8 * w, i8((i32(i16_1 + i16_2) + 128) >> 8));
            check(arm32 ? "vraddhn.i16" : "raddhn", 8 * w, u8((u32(u16_1 + u16_2) + 128) >> 8));
            check(arm32 ? "vraddhn.i32" : "raddhn", 4 * w, i16((i32_1 + i32_2 + 32768) >> 16));
            check(arm32 ? "vraddhn.i32" : "raddhn", 4 * w, u16((u64(u32_1 + u32_2) + 32768) >> 16));
            check(arm32 ? "vraddhn.i64" : "raddhn", 2 * w, i32((i64_1 + i64_2 + (Expr(int64_t(1)) << 31)) >> 32));
            // check(arm32 ? "vraddhn.i64" : "raddhn", 2 * w, u32((u128(u64_1) + u64_2 + (Expr(uint64_t(1)) << 31)) >> 32));

            // VRECPE   I, F    -       Reciprocal Estimate
            check(arm32 ? "vrecpe.f32" : "frecpe", 2 * w, fast_inverse(f32_1));

            // VRECPS   F       -       Reciprocal Step
            check(arm32 ? "vrecps.f32" : "frecps", 2 * w, fast_inverse(f32_1));

            // VREV16   X       -       Reverse in Halfwords
            // VREV32   X       -       Reverse in Words
            // VREV64   X       -       Reverse in Doublewords

            // These reverse within each halfword, word, and doubleword
            // respectively. Sometimes llvm generates them, and sometimes
            // it generates vtbl instructions.

            // VRHADD   I       -       Rounding Halving Add
            check(arm32 ? "vrhadd.s8" : "srhadd", 8 * w, i8((i16(i8_1) + i16(i8_2) + 1) / 2));
            check(arm32 ? "vrhadd.u8" : "urhadd", 8 * w, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
            check(arm32 ? "vrhadd.s16" : "srhadd", 4 * w, i16((i32(i16_1) + i32(i16_2) + 1) / 2));
            check(arm32 ? "vrhadd.u16" : "urhadd", 4 * w, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
            check(arm32 ? "vrhadd.s32" : "srhadd", 2 * w, i32((i64(i32_1) + i64(i32_2) + 1) / 2));
            check(arm32 ? "vrhadd.u32" : "urhadd", 2 * w, u32((u64(u32_1) + u64(u32_2) + 1) / 2));

            // VRSHL    I       -       Rounding Shift Left
            Expr shift_8 = (i8_2 % 8) - 4;
            Expr shift_16 = (i16_2 % 16) - 8;
            Expr shift_32 = (i32_2 % 32) - 16;
            Expr shift_64 = (i64_2 % 64) - 32;
            Expr round_s8 = (i8(1) >> min(shift_8, 0)) / 2;
            Expr round_s16 = (i16(1) >> min(shift_16, 0)) / 2;
            Expr round_s32 = (i32(1) >> min(shift_32, 0)) / 2;
            Expr round_u8 = (u8(1) >> min(shift_8, 0)) / 2;
            Expr round_u16 = (u16(1) >> min(shift_16, 0)) / 2;
            Expr round_u32 = (u32(1) >> min(shift_32, 0)) / 2;
            check(arm32 ? "vrshl.s8" : "srshl", 16 * w, i8((i16(i8_1) + round_s8) << shift_8));
            check(arm32 ? "vrshl.s16" : "srshl", 8 * w, i16((i32(i16_1) + round_s16) << shift_16));
            check(arm32 ? "vrshl.s32" : "srshl", 4 * w, i32((i32_1 + round_s32) << shift_32));
            check(arm32 ? "vrshl.u8" : "urshl", 16 * w, u8((u16(u8_1) + round_u8) << shift_8));
            check(arm32 ? "vrshl.u16" : "urshl", 8 * w, u16((u32(u16_1) + round_u16) << shift_16));
            check(arm32 ? "vrshl.u32" : "urshl", 4 * w, u32((u64(u32_1) + round_u32) << shift_32));

            round_s8 = (i8(1) << max(shift_8, 0)) / 2;
            round_s16 = (i16(1) << max(shift_16, 0)) / 2;
            round_s32 = (i32(1) << max(shift_32, 0)) / 2;
            round_u8 = (u8(1) << max(shift_8, 0)) / 2;
            round_u16 = (u16(1) << max(shift_16, 0)) / 2;
            round_u32 = (u32(1) << max(shift_32, 0)) / 2;
            check(arm32 ? "vrshl.s8" : "srshl", 16 * w, i8((i16(i8_1) + round_s8) >> shift_8));
            check(arm32 ? "vrshl.s16" : "srshl", 8 * w, i16((i32(i16_1) + round_s16) >> shift_16));
            check(arm32 ? "vrshl.s32" : "srshl", 4 * w, i32((i32_1 + round_s32) >> shift_32));
            check(arm32 ? "vrshl.u8" : "urshl", 16 * w, u8((u16(u8_1) + round_u8) >> shift_8));
            check(arm32 ? "vrshl.u16" : "urshl", 8 * w, u16((u32(u16_1) + round_u16) >> shift_16));
            check(arm32 ? "vrshl.u32" : "urshl", 4 * w, u32((u64(u32_1) + round_u32) >> shift_32));

            // VRSHR    I       -       Rounding Shift Right
            check(arm32 ? "vrshr.s8" : "srshr", 16 * w, i8((i16(i8_1) + 1) >> 1));
            check(arm32 ? "vrshr.s16" : "srshr", 8 * w, i16((i32(i16_1) + 2) >> 2));
            check(arm32 ? "vrshr.s32" : "srshr", 4 * w, i32((i32_1 + 4) >> 3));
            check(arm32 ? "vrshr.u8" : "urshr", 16 * w, u8((u16(u8_1) + 8) >> 4));
            check(arm32 ? "vrshr.u16" : "urshr", 8 * w, u16((u32(u16_1) + 16) >> 5));
            check(arm32 ? "vrshr.u32" : "urshr", 4 * w, u32((u64(u32_1) + 32) >> 6));

            // VRSHRN   I       -       Rounding Shift Right Narrow
            // LLVM14 converts RSHRN/RSHRN2 to RADDHN/RADDHN2 when the shift amount is half the width of the vector element
            // See https://reviews.llvm.org/D116166
            check(arm32 ? "vrshrn.i16" : "raddhn", 8 * w, i8((i32(i16_1) + 128) >> 8));
            check(arm32 ? "vrshrn.i32" : "rshrn", 4 * w, i16((i32_1 + 256) >> 9));
            check(arm32 ? "vrshrn.i64" : "rshrn", 2 * w, i32((i64_1 + 8) >> 4));
            check(arm32 ? "vrshrn.i16" : "raddhn", 8 * w, u8((u32(u16_1) + 128) >> 8));
            check(arm32 ? "vrshrn.i32" : "rshrn", 4 * w, u16((u64(u32_1) + 1024) >> 11));
            // check(arm32 ? "vrshrn.i64" : "raddhn", 2 * w, u32((u64_1 + 64) >> 7));

            // VRSQRTE  I, F    -       Reciprocal Square Root Estimate
            check(arm32 ? "vrsqrte.f32" : "frsqrte", 4 * w, fast_inverse_sqrt(f32_1));

            // VRSQRTS  F       -       Reciprocal Square Root Step
            check(arm32 ? "vrsqrts.f32" : "frsqrts", 4 * w, fast_inverse_sqrt(f32_1));

            // VFRINTN
            if (target.bits == 64) {
                // LLVM doesn't want to emit vfrintn on arm-32
                check(arm32 ? "vfrintn.f16" : "frintn", 8 * w, round(f16_1));
                check(arm32 ? "vfrintn.f32" : "frintn", 4 * w, round(f32_1));
                check(arm32 ? "vfrintn.f64" : "frintn", 2 * w, round(f64_1));
            }

            // VRSRA    I       -       Rounding Shift Right and Accumulate
            check(arm32 ? "vrsra.s8" : "srsra", 16 * w, i8_2 + i8((i16(i8_1) + 4) >> 3));
            check(arm32 ? "vrsra.s16" : "srsra", 8 * w, i16_2 + i16((i32(i16_1) + 8) >> 4));
            check(arm32 ? "vrsra.s32" : "srsra", 4 * w, i32_2 + ((i32_1 + 16) >> 5));
            check(arm32 ? "vrsra.u8" : "ursra", 16 * w, u8_2 + u8((u16(u8_1) + 32) >> 6));
            check(arm32 ? "vrsra.u16" : "ursra", 8 * w, u16_2 + u16((u32(u16_1) + 64) >> 7));
            check(arm32 ? "vrsra.u32" : "ursra", 4 * w, u32_2 + u32((u64(u32_1) + 128) >> 8));

            // VRSUBHN  I       -       Rounding Subtract and Narrow Returning High Half
            check(arm32 ? "vrsubhn.i16" : "rsubhn", 8 * w, i8((i32(i16_1 - i16_2) + 128) >> 8));
            check(arm32 ? "vrsubhn.i16" : "rsubhn", 8 * w, u8((u32(u16_1 - u16_2) + 128) >> 8));
            check(arm32 ? "vrsubhn.i32" : "rsubhn", 4 * w, i16((i32_1 - i32_2 + 32768) >> 16));
            check(arm32 ? "vrsubhn.i32" : "rsubhn", 4 * w, u16((u64(u32_1 - u32_2) + 32768) >> 16));
            check(arm32 ? "vrsubhn.i64" : "rsubhn", 2 * w, i32((i64_1 - i64_2 + (Expr(int64_t(1)) << 31)) >> 32));
            // check(arm32 ? "vrsubhn.i64" : "rsubhn", 2 * w, u32((u64_1 - u64_2 + (Expr(uint64_t(1)) << 31)) >> 32));

            // VSHL     I       -       Shift Left
            check(arm32 ? "vshl.i8" : "shl", 8 * w, i8_1 * 16);
            check(arm32 ? "vshl.i16" : "shl", 4 * w, i16_1 * 16);
            check(arm32 ? "vshl.i32" : "shl", 2 * w, i32_1 * 16);
            check(arm32 ? "vshl.i64" : "shl", 2 * w, i64_1 * 16);
            check(arm32 ? "vshl.i8" : "shl", 8 * w, u8_1 * 16);
            check(arm32 ? "vshl.i16" : "shl", 4 * w, u16_1 * 16);
            check(arm32 ? "vshl.i32" : "shl", 2 * w, u32_1 * 16);
            check(arm32 ? "vshl.i64" : "shl", 2 * w, u64_1 * 16);
            check(arm32 ? "vshl.s8" : "sshl", 8 * w, i8_1 << shift_8);
            check(arm32 ? "vshl.s8" : "sshl", 8 * w, i8_1 >> shift_8);
            check(arm32 ? "vshl.s16" : "sshl", 4 * w, i16_1 << shift_16);
            check(arm32 ? "vshl.s16" : "sshl", 4 * w, i16_1 >> shift_16);
            check(arm32 ? "vshl.s32" : "sshl", 2 * w, i32_1 << shift_32);
            check(arm32 ? "vshl.s32" : "sshl", 2 * w, i32_1 >> shift_32);
            check(arm32 ? "vshl.s64" : "sshl", 2 * w, i64_1 << shift_64);
            check(arm32 ? "vshl.s64" : "sshl", 2 * w, i64_1 >> shift_64);
            check(arm32 ? "vshl.u8" : "ushl", 8 * w, u8_1 << shift_8);
            check(arm32 ? "vshl.u8" : "ushl", 8 * w, u8_1 >> shift_8);
            check(arm32 ? "vshl.u16" : "ushl", 4 * w, u16_1 << shift_16);
            check(arm32 ? "vshl.u16" : "ushl", 4 * w, u16_1 >> shift_16);
            check(arm32 ? "vshl.u32" : "ushl", 2 * w, u32_1 << shift_32);
            check(arm32 ? "vshl.u32" : "ushl", 2 * w, u32_1 >> shift_32);
            check(arm32 ? "vshl.u64" : "ushl", 2 * w, u64_1 << shift_64);
            check(arm32 ? "vshl.u64" : "ushl", 2 * w, u64_1 >> shift_64);

            // VSHLL    I       -       Shift Left Long
            check(arm32 ? "vshll.s8" : "sshll", 8 * w, i16(i8_1) * 16);
            check(arm32 ? "vshll.s16" : "sshll", 4 * w, i32(i16_1) * 16);
            check(arm32 ? "vshll.s32" : "sshll", 2 * w, i64(i32_1) * 16);
            check(arm32 ? "vshll.u8" : "ushll", 8 * w, u16(u8_1) * 16);
            check(arm32 ? "vshll.u16" : "ushll", 4 * w, u32(u16_1) * 16);
            check(arm32 ? "vshll.u32" : "ushll", 2 * w, u64(u32_1) * 16);

            // VSHR     I       -       Shift Right
            check(arm32 ? "vshr.s8" : "sshr", 8 * w, i8_1 / 16);
            check(arm32 ? "vshr.s16" : "sshr", 4 * w, i16_1 / 16);
            check(arm32 ? "vshr.s32" : "sshr", 2 * w, i32_1 / 16);
            check(arm32 ? "vshr.s64" : "sshr", 2 * w, i64_1 / 16);
            check(arm32 ? "vshr.u8" : "ushr", 8 * w, u8_1 / 16);
            check(arm32 ? "vshr.u16" : "ushr", 4 * w, u16_1 / 16);
            check(arm32 ? "vshr.u32" : "ushr", 2 * w, u32_1 / 16);
            check(arm32 ? "vshr.u64" : "ushr", 2 * w, u64_1 / 16);

            // VSHRN    I       -       Shift Right Narrow
            // LLVM15 emits UZP2 if the shift amount is half the width of the vector element.
            const auto shrn_or_uzp2 = [&](int element_width, int shift_amt, int vector_width) {
                constexpr int simd_vector_bits = 128;
                if (((vector_width * element_width) % (simd_vector_bits * 2)) == 0 &&
                    shift_amt == element_width / 2) {
                    return "uzp2";
                }
                return "shrn";
            };
            check(arm32 ? "vshrn.i16" : shrn_or_uzp2(16, 8, 8 * w), 8 * w, i8(i16_1 / 256));
            check(arm32 ? "vshrn.i32" : shrn_or_uzp2(32, 16, 4 * w), 4 * w, i16(i32_1 / 65536));
            check(arm32 ? "vshrn.i64" : shrn_or_uzp2(64, 32, 2 * w), 2 * w, i32(i64_1 >> 32));
            check(arm32 ? "vshrn.i16" : shrn_or_uzp2(16, 8, 8 * w), 8 * w, u8(u16_1 / 256));
            check(arm32 ? "vshrn.i32" : shrn_or_uzp2(32, 16, 4 * w), 4 * w, u16(u32_1 / 65536));
            check(arm32 ? "vshrn.i64" : shrn_or_uzp2(64, 32, 2 * w), 2 * w, u32(u64_1 >> 32));
            check(arm32 ? "vshrn.i16" : "shrn", 8 * w, i8(i16_1 / 16));
            check(arm32 ? "vshrn.i32" : "shrn", 4 * w, i16(i32_1 / 16));
            check(arm32 ? "vshrn.i64" : "shrn", 2 * w, i32(i64_1 / 16));
            check(arm32 ? "vshrn.i16" : "shrn", 8 * w, u8(u16_1 / 16));
            check(arm32 ? "vshrn.i32" : "shrn", 4 * w, u16(u32_1 / 16));
            check(arm32 ? "vshrn.i64" : "shrn", 2 * w, u32(u64_1 / 16));

            // VSLI     X       -       Shift Left and Insert
            // I guess this could be used for (x*256) | (y & 255)? We don't do bitwise ops on integers, so skip it.

            // VSQRT    -       F, D    Square Root
            check(arm32 ? "vsqrt.f32" : "fsqrt", 4 * w, sqrt(f32_1));
            check(arm32 ? "vsqrt.f64" : "fsqrt", 2 * w, sqrt(f64_1));

            // VSRA     I       -       Shift Right and Accumulate
            check(arm32 ? "vsra.s8" : "ssra", 8 * w, i8_2 + i8_1 / 16);
            check(arm32 ? "vsra.s16" : "ssra", 4 * w, i16_2 + i16_1 / 16);
            check(arm32 ? "vsra.s32" : "ssra", 2 * w, i32_2 + i32_1 / 16);
            check(arm32 ? "vsra.s64" : "ssra", 2 * w, i64_2 + i64_1 / 16);
            check(arm32 ? "vsra.u8" : "usra", 8 * w, u8_2 + u8_1 / 16);
            check(arm32 ? "vsra.u16" : "usra", 4 * w, u16_2 + u16_1 / 16);
            check(arm32 ? "vsra.u32" : "usra", 2 * w, u32_2 + u32_1 / 16);
            check(arm32 ? "vsra.u64" : "usra", 2 * w, u64_2 + u64_1 / 16);

            // VSRI     X       -       Shift Right and Insert
            // See VSLI

            // VSUB     I, F    F, D    Subtract
            check(arm32 ? "vsub.i8" : "sub", 8 * w, i8_1 - i8_2);
            check(arm32 ? "vsub.i8" : "sub", 8 * w, u8_1 - u8_2);
            check(arm32 ? "vsub.i16" : "sub", 4 * w, i16_1 - i16_2);
            check(arm32 ? "vsub.i16" : "sub", 4 * w, u16_1 - u16_2);
            check(arm32 ? "vsub.i32" : "sub", 2 * w, i32_1 - i32_2);
            check(arm32 ? "vsub.i32" : "sub", 2 * w, u32_1 - u32_2);
            check(arm32 ? "vsub.i64" : "sub", 2 * w, i64_1 - i64_2);
            check(arm32 ? "vsub.i64" : "sub", 2 * w, u64_1 - u64_2);
            check(arm32 ? "vsub.f32" : "fsub", 4 * w, f32_1 - f32_2);
            check(arm32 ? "vsub.f32" : "fsub", 2 * w, f32_1 - f32_2);

            // VSUBHN   I       -       Subtract and Narrow
            check(arm32 ? "vsubhn.i16" : "subhn", 8 * w, i8((i16_1 - i16_2) / 256));
            check(arm32 ? "vsubhn.i16" : "subhn", 8 * w, u8((u16_1 - u16_2) / 256));
            check(arm32 ? "vsubhn.i32" : "subhn", 4 * w, i16((i32_1 - i32_2) / 65536));
            check(arm32 ? "vsubhn.i32" : "subhn", 4 * w, u16((u32_1 - u32_2) / 65536));
            check(arm32 ? "vsubhn.i64" : "subhn", 2 * w, i32((i64_1 - i64_2) >> 32));
            check(arm32 ? "vsubhn.i64" : "subhn", 2 * w, u32((u64_1 - u64_2) >> 32));

            // VSUBL    I       -       Subtract Long
            check(arm32 ? "vsubl.s8" : "ssubl", 8 * w, i16(i8_1) - i16(i8_2));
            check(arm32 ? "vsubl.u8" : "usubl", 8 * w, u16(u8_1) - u16(u8_2));
            check(arm32 ? "vsubl.s16" : "ssubl", 4 * w, i32(i16_1) - i32(i16_2));
            check(arm32 ? "vsubl.u16" : "usubl", 4 * w, u32(u16_1) - u32(u16_2));
            check(arm32 ? "vsubl.s32" : "ssubl", 2 * w, i64(i32_1) - i64(i32_2));
            check(arm32 ? "vsubl.u32" : "usubl", 2 * w, u64(u32_1) - u64(u32_2));

            check(arm32 ? "vsubl.s8" : "ssubl", 8 * w, i16(i8_1) - i16(in_i8(0)));
            check(arm32 ? "vsubl.u8" : "usubl", 8 * w, u16(u8_1) - u16(in_u8(0)));
            check(arm32 ? "vsubl.s16" : "ssubl", 4 * w, i32(i16_1) - i32(in_i16(0)));
            check(arm32 ? "vsubl.u16" : "usubl", 4 * w, u32(u16_1) - u32(in_u16(0)));
            check(arm32 ? "vsubl.s32" : "ssubl", 2 * w, i64(i32_1) - i64(in_i32(0)));
            check(arm32 ? "vsubl.u32" : "usubl", 2 * w, u64(u32_1) - u64(in_u32(0)));

            // VSUBW    I       -       Subtract Wide
            check(arm32 ? "vsubw.s8" : "ssubw", 8 * w, i16_1 - i8_1);
            check(arm32 ? "vsubw.u8" : "usubw", 8 * w, u16_1 - u8_1);
            check(arm32 ? "vsubw.s16" : "ssubw", 4 * w, i32_1 - i16_1);
            check(arm32 ? "vsubw.u16" : "usubw", 4 * w, u32_1 - u16_1);
            check(arm32 ? "vsubw.s32" : "ssubw", 2 * w, i64_1 - i32_1);
            check(arm32 ? "vsubw.u32" : "usubw", 2 * w, u64_1 - u32_1);
        }

        // VST2 X       -       Store two-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 128; width <= 128 * 4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits * 2) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x % 2 == 0, tmp1(x / 2), tmp1(x / 2 + 16));
                    tmp2.compute_root().vectorize(x, width / bits);
                    std::string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : "st2", width / bits, tmp2(0, 0) + tmp2(0, 63));
                }
            }
        }

        // Also check when the two expressions interleaved have a common
        // subexpression, which results in a vector var being lifted out.
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 128; width <= 128 * 4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits * 2) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    Expr e = (tmp1(x / 2) * 2 + 7) / 4;
                    tmp2(x, y) = select(x % 2 == 0, e * 3, e + 17);
                    tmp2.compute_root().vectorize(x, width / bits);
                    std::string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : "st2", width / bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VST3 X       -       Store three-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 192; width <= 192 * 4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits * 3) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x % 3 == 0, tmp1(x / 3),
                                        x % 3 == 1, tmp1(x / 3 + 16),
                                        tmp1(x / 3 + 32));
                    tmp2.compute_root().vectorize(x, width / bits);
                    std::string op = "vst3." + std::to_string(bits);
                    check(arm32 ? op : "st3", width / bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VST4 X       -       Store four-element structures
        for (int sign = 0; sign <= 1; sign++) {
            for (int width = 256; width <= 256 * 4; width *= 2) {
                for (int bits = 8; bits < 64; bits *= 2) {
                    if (width <= bits * 4) continue;
                    Func tmp1, tmp2;
                    tmp1(x) = cast(sign ? Int(bits) : UInt(bits), x);
                    tmp1.compute_root();
                    tmp2(x, y) = select(x % 4 == 0, tmp1(x / 4),
                                        x % 4 == 1, tmp1(x / 4 + 16),
                                        x % 4 == 2, tmp1(x / 4 + 32),
                                        tmp1(x / 4 + 48));
                    tmp2.compute_root().vectorize(x, width / bits);
                    std::string op = "vst4." + std::to_string(bits);
                    check(arm32 ? op : "st4", width / bits, tmp2(0, 0) + tmp2(0, 127));
                }
            }
        }

        // VSTM X       F, D    Store Multiple Registers
        // VSTR X       F, D    Store Register
        // we trust llvm to use these

        // VSWP I       -       Swap Contents
        // Swaps the contents of two registers. Not sure why this would be useful.

        // VTBL X       -       Table Lookup
        // Arm's version of shufps. Allows for arbitrary permutations of a
        // 64-bit vector. We typically use vrev variants instead.

        // VTBX X       -       Table Extension
        // Like vtbl, but doesn't change any elements where the index was
        // out of bounds. Not sure how we'd use this.

        // VTRN X       -       Transpose
        // Swaps the even elements of one vector with the odd elements of
        // another. Not useful for us.

        // VTST I       -       Test Bits
        // check("vtst.32", 4, (bool1 & bool2) != 0);

        // VUZP X       -       Unzip
        // VZIP X       -       Zip
        // Interleave or deinterleave two vectors. Given that we use
        // interleaving loads and stores, it's hard to hit this op with
        // halide.
    }

private:
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckARM>(
        argc, argv,
        {
            Target("arm-32-linux"),
            Target("arm-64-linux"),
            Target("arm-64-linux-arm_dot_prod"),
        });
}

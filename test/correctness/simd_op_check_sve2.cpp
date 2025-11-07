#include "simd_op_check.h"

#include "Halide.h"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>

using namespace Halide;
using namespace Halide::ConciseCasts;
using namespace std;

namespace {

using CastFuncTy = function<Expr(Expr)>;

class SimdOpCheckArmSve : public SimdOpCheckTest {
public:
    SimdOpCheckArmSve(Target t, int w = 384, int h = 32)
        : SimdOpCheckTest(t, w, h), debug_mode(Internal::get_env_variable("HL_DEBUG_SIMDOPCHECK")) {

        // Determine and hold can_run_the_code
        // TODO: Since features of Arm CPU cannot be obtained automatically from get_host_target(),
        // it is necessary to set some feature (e.g. "arm_fp16") explicitly to HL_JIT_TARGET.
        // Halide throws error if there is unacceptable mismatch between jit_target and host_target.

        Target host = get_host_target();
        Target jit_target = get_jit_target_from_environment();
        cout << "host is:          " << host.to_string() << endl;
        cout << "HL_TARGET is:     " << target.to_string() << endl;
        cout << "HL_JIT_TARGET is: " << jit_target.to_string() << endl;

        auto is_same_triple = [](const Target &t1, const Target &t2) -> bool {
            return t1.arch == t2.arch && t1.bits == t2.bits && t1.os == t2.os && t1.vector_bits == t2.vector_bits;
        };

        can_run_the_code = is_same_triple(host, target) && is_same_triple(jit_target, target);

        // A bunch of feature flags also need to match between the
        // compiled code and the host in order to run the code.
        for (Target::Feature f : {Target::ARMv7s, Target::ARMFp16, Target::NoNEON, Target::SVE2}) {
            if (target.has_feature(f) != jit_target.has_feature(f)) {
                can_run_the_code = false;
            }
        }
        if (!can_run_the_code) {
            cout << "[WARN] To perform verification of realization, "
                 << R"(the target triple "arm-<bits>-<os>" and key feature "arm_fp16")"
                 << " must be the same between HL_TARGET and HL_JIT_TARGET" << endl;
        }
    }

    bool can_run_code() const override {
        // If we can meet the condition about target, run the error checking Halide::Func.
        return can_run_the_code;
    }

    void add_tests() override {
        check_arm_integer();
        check_arm_float();
        check_arm_load_store();
        check_arm_pairwise();
    }

private:
    void check_arm_integer() {
        // clang-format off
        vector<tuple<int, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy,
                     CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy,
                     CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy>> test_params{
            {8,  in_i8,  in_u8,  in_f16, in_i16, in_u16, i8,  i8_sat,  i16, i8,  i8_sat,  u8,  u8_sat,  u16, u8,  u8_sat},
            {16, in_i16, in_u16, in_f16, in_i32, in_u32, i16, i16_sat, i32, i8,  i8_sat,  u16, u16_sat, u32, u8,  u8_sat},
            {32, in_i32, in_u32, in_f32, in_i64, in_u64, i32, i32_sat, i64, i16, i16_sat, u32, u32_sat, u64, u16, u16_sat},
            {64, in_i64, in_u64, in_f64, in_i64, in_u64, i64, i64_sat, i64, i32, i32_sat, u64, u64_sat, u64, u32, u32_sat},
        };
        // clang-format on

        for (const auto &[bits, in_i, in_u, in_f, in_i_wide, in_u_wide,
                          cast_i, satcast_i, widen_i, narrow_i, satnarrow_i,
                          cast_u, satcast_u, widen_u, narrow_u, satnarrow_u] : test_params) {

            Expr i_1 = in_i(x), i_2 = in_i(x + 16), i_3 = in_i(x + 32);
            Expr u_1 = in_u(x), u_2 = in_u(x + 16), u_3 = in_u(x + 32);
            Expr i_wide_1 = in_i_wide(x), i_wide_2 = in_i_wide(x + 16);
            Expr u_wide_1 = in_u_wide(x), u_wide_2 = in_u_wide(x + 16);
            Expr f_1 = in_f(x);

            // TODO: reconcile this comment and logic and figure out
            // whether we're test 192 and 256 for NEON and which bit
            // widths other that the target one for SVE2.
            //
            // In general neon ops have the 64-bit version, the 128-bit
            // version (ending in q), and the widening version that takes
            // 64-bit args and produces a 128-bit result (ending in l). We try
            // to peephole match any with vector, so we just try 64-bits, 128
            // bits, 192 bits, and 256 bits for everything.
            std::vector<int> simd_bit_widths;
            if (has_neon()) {
                simd_bit_widths.push_back(64);
                simd_bit_widths.push_back(128);
            }
            if (has_sve() && ((target.vector_bits > 128) || !has_neon())) {
                simd_bit_widths.push_back(target.vector_bits);
            }
            for (auto &total_bits : simd_bit_widths) {
                const int vf = total_bits / bits;

                // Due to workaround for SVE LLVM issues, in case of vector of half length of natural_lanes,
                // there is some inconsistency in generated SVE insturction about the number of lanes.
                // So the verification of lanes is skipped for this specific case.
                const int instr_lanes = (total_bits == 64 && has_sve()) ?
                                            Instruction::ANY_LANES :
                                            Instruction::get_instr_lanes(bits, vf, target);
                const int widen_lanes = Instruction::get_instr_lanes(bits * 2, vf, target);
                const int narrow_lanes = Instruction::get_instr_lanes(bits, vf * 2, target);

                AddTestFunctor add_all(*this, bits, instr_lanes, vf);
                AddTestFunctor add_all_vec(*this, bits, instr_lanes, vf, vf != 1);
                AddTestFunctor add_8_16_32(*this, bits, instr_lanes, vf, bits != 64);
                AddTestFunctor add_16_32_64(*this, bits, instr_lanes, vf, bits != 8);
                AddTestFunctor add_16_32(*this, bits, instr_lanes, vf, bits == 16 || bits == 32);
                AddTestFunctor add_32(*this, bits, instr_lanes, vf, bits == 32);

                AddTestFunctor add_8_16_32_widen(*this, bits, widen_lanes, vf, bits != 64 && !has_sve());

                AddTestFunctor add_16_32_64_narrow(*this, bits, narrow_lanes, vf * 2, bits != 8 && !has_sve());
                AddTestFunctor add_16_32_narrow(*this, bits, narrow_lanes, vf * 2, (bits == 16 || bits == 32) && !has_sve());
                AddTestFunctor add_16_narrow(*this, bits, narrow_lanes, vf * 2, bits == 16 && !has_sve());

                // VABA     I       -       Absolute Difference and Accumulate
                if (!has_sve()) {
                    // Relying on LLVM to detect accumulation
                    add_8_16_32(sel_op("vaba.s", "saba"), i_1 + absd(i_2, i_3));
                    add_8_16_32(sel_op("vaba.u", "uaba"), u_1 + absd(u_2, u_3));
                }

                // VABAL    I       -       Absolute Difference and Accumulate Long
                add_8_16_32_widen(sel_op("vabal.s", "sabal"), i_wide_1 + absd(i_2, i_3));
                add_8_16_32_widen(sel_op("vabal.u", "uabal"), u_wide_1 + absd(u_2, u_3));

                // VABD     I, F    -       Absolute Difference
                add_8_16_32(sel_op("vabd.s", "sabd"), absd(i_2, i_3));
                add_8_16_32(sel_op("vabd.u", "uabd"), absd(u_2, u_3));

                // Via widening, taking abs, then narrowing
                add_8_16_32(sel_op("vabd.s", "sabd"), cast_u(abs(widen_i(i_2) - i_3)));
                add_8_16_32(sel_op("vabd.u", "uabd"), cast_u(abs(widen_i(u_2) - u_3)));

                // VABDL    I       -       Absolute Difference Long
                add_8_16_32_widen(sel_op("vabdl.s", "sabdl"), widen_i(absd(i_2, i_3)));
                add_8_16_32_widen(sel_op("vabdl.u", "uabdl"), widen_u(absd(u_2, u_3)));

                // Via widening then taking an abs
                add_8_16_32_widen(sel_op("vabdl.s", "sabdl"), abs(widen_i(i_2) - widen_i(i_3)));
                add_8_16_32_widen(sel_op("vabdl.u", "uabdl"), abs(widen_i(u_2) - widen_i(u_3)));

                // VABS     I, F    F, D    Absolute
                add_8_16_32(sel_op("vabs.s", "abs"), abs(i_1));

                // VADD     I, F    F, D    Add
                add_all_vec(sel_op("vadd.i", "add"), i_1 + i_2);
                add_all_vec(sel_op("vadd.i", "add"), u_1 + u_2);

                // VADDHN   I       -       Add and Narrow Returning High Half
                add_16_32_64_narrow(sel_op("vaddhn.i", "addhn"), narrow_i((i_1 + i_2) >> (bits / 2)));
                add_16_32_64_narrow(sel_op("vaddhn.i", "addhn"), narrow_u((u_1 + u_2) >> (bits / 2)));

                // VADDL    I       -       Add Long
                add_8_16_32_widen(sel_op("vaddl.s", "saddl"), widen_i(i_1) + widen_i(i_2));
                add_8_16_32_widen(sel_op("vaddl.u", "uaddl"), widen_u(u_1) + widen_u(u_2));

                // VADDW    I       -       Add Wide
                add_8_16_32_widen(sel_op("vaddw.s", "saddw"), i_1 + i_wide_1);
                add_8_16_32_widen(sel_op("vaddw.u", "uaddw"), u_1 + u_wide_1);

                // VAND     X       -       Bitwise AND
                // Not implemented in front-end yet
                // VBIC     I       -       Bitwise Clear
                // VBIF     X       -       Bitwise Insert if False
                // VBIT     X       -       Bitwise Insert if True
                // skip these ones

                // VCEQ     I, F    -       Compare Equal
                add_8_16_32(sel_op("vceq.i", "cmeq", "cmpeq"), select(i_1 == i_2, cast_i(1), cast_i(2)));
                add_8_16_32(sel_op("vceq.i", "cmeq", "cmpeq"), select(u_1 == u_2, cast_u(1), cast_u(2)));
#if 0
                // VCGE     I, F    -       Compare Greater Than or Equal
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
                add_8_16_32(sel_op("vcgt.s", "cmgt", "cmpgt"), select(i_1 > i_2, cast_i(1), cast_i(2)));
                add_8_16_32(sel_op("vcgt.u", "cmhi", "cmphi"), select(u_1 > u_2, cast_u(1), cast_u(2)));
#if 0
                // VCLS     I       -       Count Leading Sign Bits
                // We don't currently match these, but it wouldn't be hard to do.
                check(arm32 ? "vcls.s8" : "cls", 8 * w, max(count_leading_zeros(i8_1), count_leading_zeros(~i8_1)));
                check(arm32 ? "vcls.s16" : "cls", 8 * w, max(count_leading_zeros(i16_1), count_leading_zeros(~i16_1)));
                check(arm32 ? "vcls.s32" : "cls", 8 * w, max(count_leading_zeros(i32_1), count_leading_zeros(~i32_1)));
#endif
                // VCLZ     I       -       Count Leading Zeros
                add_8_16_32(sel_op("vclz.i", "clz"), count_leading_zeros(i_1));
                add_8_16_32(sel_op("vclz.i", "clz"), count_leading_zeros(u_1));

                // VCMP     -       F, D    Compare Setting Flags
                // We skip this

                // VCNT     I       -       Count Number of Set Bits
                if (!has_sve()) {
                    // In NEON, there is only cnt for bytes, and then horizontal adds.
                    add_8_16_32({{sel_op("vcnt.", "cnt"), 8, total_bits == 64 ? 8 : 16}}, vf, popcount(i_1));
                    add_8_16_32({{sel_op("vcnt.", "cnt"), 8, total_bits == 64 ? 8 : 16}}, vf, popcount(u_1));
                } else {
                    add_8_16_32("cnt", popcount(i_1));
                    add_8_16_32("cnt", popcount(u_1));
                }

                // VDUP     X       -       Duplicate
                add_8_16_32(sel_op("vdup.", "dup", "mov"), cast_i(y));
                add_8_16_32(sel_op("vdup.", "dup", "mov"), cast_u(y));

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
                add_8_16_32(sel_op("vhadd.s", "shadd"), cast_i((widen_i(i_1) + widen_i(i_2)) / 2));
                add_8_16_32(sel_op("vhadd.u", "uhadd"), cast_u((widen_u(u_1) + widen_u(u_2)) / 2));

                // Halide doesn't define overflow behavior for i32 so we
                // can use vhadd instruction. We can't use it for unsigned u8,i16,u16,u32.
                add_32(sel_op("vhadd.s", "shadd"), (i_1 + i_2) / 2);

                // VHSUB    I       -       Halving Subtract
                add_8_16_32(sel_op("vhsub.s", "shsub"), cast_i((widen_i(i_1) - widen_i(i_2)) / 2));
                add_8_16_32(sel_op("vhsub.u", "uhsub"), cast_u((widen_u(u_1) - widen_u(u_2)) / 2));

                add_32(sel_op("vhsub.s", "shsub"), (i_1 - i_2) / 2);

                // VMAX     I, F    -       Maximum
                add_8_16_32(sel_op("vmax.s", "smax"), max(i_1, i_2));
                add_8_16_32(sel_op("vmax.u", "umax"), max(u_1, u_2));

                // VMIN     I, F    -       Minimum
                add_8_16_32(sel_op("vmin.s", "smin"), min(i_1, i_2));
                add_8_16_32(sel_op("vmin.u", "umin"), min(u_1, u_2));

                // VMLA     I, F    F, D    Multiply Accumulate
                add_8_16_32("mla signed", sel_op("vmla.i", "mla", "(mad|mla)"), i_1 + i_2 * i_3);
                add_8_16_32("mla unsigned", sel_op("vmla.i", "mla", "(mad|mla)"), u_1 + u_2 * u_3);
                // VMLS     I, F    F, D    Multiply Subtract
                add_8_16_32("mls signed", sel_op("vmls.i", "mls", "(mls|msb)"), i_1 - i_2 * i_3);
                add_8_16_32("mls unsigned", sel_op("vmls.i", "mls", "(mls|msb)"), u_1 - u_2 * u_3);

                // VMLAL    I       -       Multiply Accumulate Long
                // Try to trick LLVM into generating a zext instead of a sext by making
                // LLVM think the operand never has a leading 1 bit. zext breaks LLVM's
                // pattern matching of mlal.
                add_8_16_32_widen(sel_op("vmlal.s", "smlal"), i_wide_1 + widen_i(i_2 & 0x3) * i_3);
                add_8_16_32_widen(sel_op("vmlal.u", "umlal"), u_wide_1 + widen_u(u_2) * u_3);

                // VMLSL    I       -       Multiply Subtract Long
                add_8_16_32_widen(sel_op("vmlsl.s", "smlsl"), i_wide_1 - widen_i(i_2 & 0x3) * i_3);
                add_8_16_32_widen(sel_op("vmlsl.u", "umlsl"), u_wide_1 - widen_u(u_2) * u_3);

                // VMOV     X       F, D    Move Register or Immediate
                // This is for loading immediates, which we won't do in the inner loop anyway

                // VMOVL    I       -       Move Long
                // For aarch64, llvm does a widening shift by 0 instead of using the sxtl instruction.
                add_8_16_32_widen(sel_op("vmovl.s", "sshll"), widen_i(i_1));
                add_8_16_32_widen(sel_op("vmovl.u", "ushll"), widen_u(u_1));
                add_8_16_32_widen(sel_op("vmovl.u", "ushll"), widen_i(u_1));

                // VMOVN    I       -       Move and Narrow
                if (total_bits >= 128) {
                    if (is_arm32()) {
                        add_16_32_64_narrow("vmovn.i", narrow_i(i_1));
                        add_16_32_64_narrow("vmovn.i", narrow_u(u_1));
                    } else {
                        add_16_32_64({{"uzp1", bits / 2, narrow_lanes * 2}}, vf * 2, narrow_i(i_1));
                        add_16_32_64({{"uzp1", bits / 2, narrow_lanes * 2}}, vf * 2, narrow_u(u_1));
                    }
                } else {
                    add_16_32_64_narrow(sel_op("vmovn.i", "xtn"), narrow_i(i_1));
                    add_16_32_64_narrow(sel_op("vmovn.i", "xtn"), narrow_u(u_1));
                }

                // VMRS     X       F, D    Move Advanced SIMD or VFP Register to ARM compute Engine
                // VMSR     X       F, D    Move ARM Core Register to Advanced SIMD or VFP
                // trust llvm to use this correctly

                // VMUL     I, F, P F, D    Multiply
                add_8_16_32(sel_op("vmul.i", "mul"), i_2 * i_1);
                add_8_16_32(sel_op("vmul.i", "mul"), u_2 * u_1);

                // VMULL    I, F, P -       Multiply Long
                add_8_16_32_widen(sel_op("vmull.s", "smull"), widen_i(i_1) * i_2);
                add_8_16_32_widen(sel_op("vmull.u", "umull"), widen_u(u_1) * u_2);

                // integer division by a constant should use fixed point unsigned
                // multiplication, which is done by using a widening multiply
                // followed by a narrowing
                add_8_16_32_widen(sel_op("vmull.u", "umull"), i_1 / 37);
                add_8_16_32_widen(sel_op("vmull.u", "umull"), u_1 / 37);

                // VMVN     X       -       Bitwise NOT
                // check("vmvn", ~bool1);

                // VNEG     I, F    F, D    Negate
                add_8_16_32(sel_op("vneg.s", "neg"), -i_1);

#if 0
                // These are vfp, not neon. They only work on scalars
                check("vnmla.f32", 4, -(f32_1 + f32_2*f32_3));
                check("vnmla.f64", 2, -(f64_1 + f64_2*f64_3));
                check("vnmls.f32", 4, -(f32_1 - f32_2*f32_3));
                check("vnmls.f64", 2, -(f64_1 - f64_2*f64_3));
                check("vnmul.f32", 4, -(f32_1*f32_2));
                check("vnmul.f64", 2, -(f64_1*f64_2));

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
                add_8_16_32(sel_op("vqadd.s", "sqadd"), satcast_i(widen_i(i_1) + widen_i(i_2)));
                const Expr max_u = UInt(bits).max();
                add_8_16_32(sel_op("vqadd.u", "uqadd"), cast_u(min(widen_u(u_1) + widen_u(u_2), max_u)));

                // Check the case where we add a constant that could be narrowed
                add_8_16_32(sel_op("vqadd.u", "uqadd"), cast_u(min(widen_u(u_1) + 17, max_u)));

                // Can't do larger ones because we can't represent the intermediate 128-bit wide ops.

                // VQDMLAL  I       -       Saturating Double Multiply Accumulate Long
                // VQDMLSL  I       -       Saturating Double Multiply Subtract Long
                // We don't do these, but it would be possible.

                // VQDMULH  I       -       Saturating Doubling Multiply Returning High Half
                // VQDMULL  I       -       Saturating Doubling Multiply Long
                add_16_32(sel_op("vqdmulh.s", "sqdmulh"), satcast_i((widen_i(i_1) * widen_i(i_2)) >> (bits - 1)));

                // VQMOVN   I       -       Saturating Move and Narrow
                // VQMOVUN  I       -       Saturating Move and Unsigned Narrow
                add_16_32_64_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(i_1));
                add_16_32_64_narrow(sel_op("vqmovun.s", "sqxtun"), satnarrow_u(i_1));
                const Expr max_u_narrow = UInt(bits / 2).max();
                add_16_32_64_narrow(sel_op("vqmovn.u", "uqxtn"), narrow_u(min(u_1, max_u_narrow)));
                // Double saturating narrow
                add_16_32_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(i_wide_1));
                add_16_32_narrow(sel_op("vqmovn.u", "uqxtn"), narrow_u(min(u_wide_1, max_u_narrow)));
                add_16_32_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(i_wide_1));
                add_16_32_narrow(sel_op("vqmovun.s", "sqxtun"), satnarrow_u(i_wide_1));
                // Triple saturating narrow
                Expr i64_1 = in_i64(x), u64_1 = in_u64(x), f32_1 = in_f32(x), f64_1 = in_f64(x);
                add_16_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(i64_1));
                add_16_narrow(sel_op("vqmovn.u", "uqxtn"), narrow_u(min(u64_1, max_u_narrow)));
                add_16_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(f32_1));
                add_16_narrow(sel_op("vqmovn.s", "sqxtn"), satnarrow_i(f64_1));
                add_16_narrow(sel_op("vqmovun.s", "sqxtun"), satnarrow_u(f32_1));
                add_16_narrow(sel_op("vqmovun.s", "sqxtun"), satnarrow_u(f64_1));

                // VQNEG    I       -       Saturating Negate
                const Expr max_i = Int(bits).max();
                add_8_16_32(sel_op("vqneg.s", "sqneg"), -max(i_1, -max_i));

                // VQRDMULH I       -       Saturating Rounding Doubling Multiply Returning High Half
                // Note: division in Halide always rounds down (not towards
                // zero). Otherwise these patterns would be more complicated.
                add_16_32(sel_op("vqrdmulh.s", "sqrdmulh"), satcast_i((widen_i(i_1) * widen_i(i_2) + (1 << (bits - 2))) / (widen_i(1) << (bits - 1))));

                // VQRSHRN   I       -       Saturating Rounding Shift Right Narrow
                // VQRSHRUN  I       -       Saturating Rounding Shift Right Unsigned Narrow
                add_16_32_64_narrow(sel_op("vqrshrn.s", "sqrshrn"), satnarrow_i((widen_i(i_1) + 8) / 16));
                add_16_32_64_narrow(sel_op("vqrshrun.s", "sqrshrun"), satnarrow_u((widen_i(i_1) + 8) / 16));
                add_16_32_narrow(sel_op("vqrshrn.u", "uqrshrn"), narrow_u(min((widen_u(u_1) + 8) / 16, max_u_narrow)));

                // VQSHL    I       -       Saturating Shift Left
                add_8_16_32(sel_op("vqshl.s", "sqshl"), satcast_i(widen_i(i_1) * 16));
                add_8_16_32(sel_op("vqshl.u", "uqshl"), cast_u(min(widen_u(u_1) * 16, max_u)));

                // VQSHLU   I       -       Saturating Shift Left Unsigned
                if (!has_sve()) {
                    add_8_16_32(sel_op("vqshlu.s", "sqshlu"), satcast_u(widen_i(i_1) * 16));
                }

                // VQSHRN   I       -       Saturating Shift Right Narrow
                // VQSHRUN  I       -       Saturating Shift Right Unsigned Narrow
                add_16_32_64_narrow(sel_op("vqshrn.s", "sqshrn"), satnarrow_i(i_1 / 16));
                add_16_32_64_narrow(sel_op("vqshrun.s", "sqshrun"), satnarrow_u(i_1 / 16));
                add_16_32_narrow(sel_op("vqshrn.u", "uqshrn"), narrow_u(min(u_1 / 16, max_u_narrow)));

                // VQSUB    I       -       Saturating Subtract
                add_8_16_32(sel_op("vqsub.s", "sqsub"), satcast_i(widen_i(i_1) - widen_i(i_2)));

                // N.B. Saturating subtracts are expressed by widening to a igned* type
                add_8_16_32(sel_op("vqsub.u", "uqsub"), satcast_u(widen_i(u_1) - widen_i(u_2)));

                // VRADDHN  I       -       Rounding Add and Narrow Returning High Half
                add_16_32_64_narrow(sel_op("vraddhn.i", "raddhn"), narrow_i((widen_i(i_1 + i_2) + (Expr(cast_i(1)) << (bits / 2 - 1))) >> (bits / 2)));
                add_16_32_narrow(sel_op("vraddhn.i", "raddhn"), narrow_u((widen_u(u_1 + u_2) + (Expr(cast_u(1)) << (bits / 2 - 1))) >> (bits / 2)));

                // VREV16   X       -       Reverse in Halfwords
                // VREV32   X       -       Reverse in Words
                // VREV64   X       -       Reverse in Doublewords

                // These reverse within each halfword, word, and doubleword
                // respectively. Sometimes llvm generates them, and sometimes
                // it generates vtbl instructions.

                // VRHADD   I       -       Rounding Halving Add
                add_8_16_32(sel_op("vrhadd.s", "srhadd"), cast_i((widen_i(i_1) + widen_i(i_2) + 1) / 2));
                add_8_16_32(sel_op("vrhadd.u", "urhadd"), cast_u((widen_u(u_1) + widen_u(u_2) + 1) / 2));

                // VRSHL    I       -       Rounding Shift Left
                Expr shift = (i_2 % bits) - (bits / 2);
                Expr round_s = (cast_i(1) >> min(shift, 0)) / 2;
                Expr round_u = (cast_u(1) >> min(shift, 0)) / 2;
                add_8_16_32(sel_op("vrshl.s", "srshl", "srshlr"), cast_i((widen_i(i_1) + round_s) << shift));
                add_8_16_32(sel_op("vrshl.u", "urshl", "urshlr"), cast_u((widen_u(u_1) + round_u) << shift));

                round_s = (cast_i(1) << max(shift, 0)) / 2;
                round_u = (cast_u(1) << max(shift, 0)) / 2;
                add_8_16_32(sel_op("vrshl.s", "srshl", "srshlr"), cast_i((widen_i(i_1) + round_s) >> shift));
                add_8_16_32(sel_op("vrshl.u", "urshl", "urshlr"), cast_u((widen_u(u_1) + round_u) >> shift));

                // VRSHR    I       -       Rounding Shift Right
                add_8_16_32(sel_op("vrshr.s", "srshr", "srshl"), cast_i((widen_i(i_1) + 1) >> 1));
                add_8_16_32(sel_op("vrshr.u", "urshr", "urshl"), cast_u((widen_u(u_1) + 1) >> 1));

                // VRSHRN   I       -       Rounding Shift Right Narrow
                // LLVM14 converts RSHRN/RSHRN2 to RADDHN/RADDHN2 when the shift amount is half the width of the vector element
                // See https://reviews.llvm.org/D116166
                add_16_32_narrow(sel_op("vrshrn.i", "raddhn"), narrow_i((widen_i(i_1) + (cast_i(1) << (bits / 2 - 1))) >> (bits / 2)));
                add_16_32_narrow(sel_op("vrshrn.i", "raddhn"), narrow_u((widen_u(u_1) + (cast_u(1) << (bits / 2 - 1))) >> (bits / 2)));
                add_16_32_64_narrow(sel_op("vrshrn.i", "rshrn"), narrow_i((widen_i(i_1) + (1 << (bits / 4))) >> (bits / 4 + 1)));
                add_16_32_narrow(sel_op("vrshrn.i", "rshrn"), narrow_u((widen_u(u_1) + (1 << (bits / 4))) >> (bits / 4 + 1)));

                // VRSRA    I       -       Rounding Shift Right and Accumulate
                if (!has_sve()) {
                    // Relying on LLVM to detect accumulation
                    add_8_16_32(sel_op("vrsra.s", "srsra"), i_2 + cast_i((widen_i(i_1) + 1) >> 1));
                    add_8_16_32(sel_op("vrsra.u", "ursra"), i_2 + cast_u((widen_u(u_1) + 1) >> 1));
                }

                // VRSUBHN  I       -       Rounding Subtract and Narrow Returning High Half
                add_16_32_64_narrow(sel_op("vrsubhn.i", "rsubhn"), narrow_i((widen_i(i_1 - i_2) + (Expr(cast_i(1)) << (bits / 2 - 1))) >> (bits / 2)));
                add_16_32_narrow(sel_op("vrsubhn.i", "rsubhn"), narrow_u((widen_u(u_1 - u_2) + (Expr(cast_u(1)) << (bits / 2 - 1))) >> (bits / 2)));

                // VSHL     I       -       Shift Left
                add_all_vec(sel_op("vshl.i", "shl", "lsl"), i_1 * 16);
                add_all_vec(sel_op("vshl.i", "shl", "lsl"), u_1 * 16);

                if (!has_sve()) {  // No equivalent instruction in SVE.
                    add_all_vec(sel_op("vshl.s", "sshl"), i_1 << shift);
                    add_all_vec(sel_op("vshl.s", "sshl"), i_1 >> shift);
                    add_all_vec(sel_op("vshl.u", "ushl"), u_1 << shift);
                    add_all_vec(sel_op("vshl.u", "ushl"), u_1 >> shift);
                }

                // VSHLL    I       -       Shift Left Long
                add_8_16_32_widen(sel_op("vshll.s", "sshll"), widen_i(i_1) * 16);
                add_8_16_32_widen(sel_op("vshll.u", "ushll"), widen_u(u_1) * 16);

                // VSHR     I       -       Shift Right
                add_all_vec(sel_op("vshr.s", "sshr", "asr"), i_1 / 16);
                add_all_vec(sel_op("vshr.u", "ushr", "lsr"), u_1 / 16);

                // VSHRN    I       -       Shift Right Narrow
                add_16_32_64_narrow(sel_op("vshrn.i", "shrn"), narrow_i(i_1 >> (bits / 2)));
                add_16_32_64_narrow(sel_op("vshrn.i", "shrn"), narrow_u(u_1 >> (bits / 2)));

                add_16_32_64_narrow(sel_op("vshrn.i", "shrn"), narrow_i(i_1 / 16));
                add_16_32_64_narrow(sel_op("vshrn.i", "shrn"), narrow_u(u_1 / 16));

                // VSLI     X       -       Shift Left and Insert
                // I guess this could be used for (x*256) | (y & 255)? We don't do bitwise ops on integers, so skip it.

                // VSRA     I       -       Shift Right and Accumulate
                if (!has_sve()) {
                    // Relying on LLVM to detect accumulation
                    add_all_vec(sel_op("vsra.s", "ssra"), i_2 + i_1 / 16);
                    add_all_vec(sel_op("vsra.u", "usra"), u_2 + u_1 / 16);
                }

                // VSRI     X       -       Shift Right and Insert
                // See VSLI

                // VSUB     I, F    F, D    Subtract
                add_all_vec(sel_op("vsub.i", "sub"), i_1 - i_2);
                add_all_vec(sel_op("vsub.i", "sub"), u_1 - u_2);

                // VSUBHN   I       -       Subtract and Narrow
                add_16_32_64_narrow(sel_op("vsubhn.i", "subhn"), narrow_i((i_1 - i_2) >> (bits / 2)));
                add_16_32_64_narrow(sel_op("vsubhn.i", "subhn"), narrow_u((u_1 - u_2) >> (bits / 2)));

                // VSUBL    I       -       Subtract Long
                add_8_16_32_widen(sel_op("vsubl.s", "ssubl"), widen_i(i_1) - widen_i(i_2));
                add_8_16_32_widen(sel_op("vsubl.u", "usubl"), widen_u(u_1) - widen_u(u_2));

                add_8_16_32_widen(sel_op("vsubl.s", "ssubl"), widen_i(i_1) - widen_i(in_i(0)));
                add_8_16_32_widen(sel_op("vsubl.u", "usubl"), widen_u(u_1) - widen_u(in_u(0)));

                // VSUBW    I       -       Subtract Wide
                add_8_16_32_widen(sel_op("vsubw.s", "ssubw"), i_wide_1 - i_1);
                add_8_16_32_widen(sel_op("vsubw.u", "usubw"), u_wide_1 - u_1);
            }
        }
    }

    void check_arm_float() {
        vector<tuple<int, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy>> test_params{
            {16, in_f16, in_u16, in_i16, f16},
            {32, in_f32, in_u32, in_i32, f32},
            {64, in_f64, in_u64, in_i64, f64},
        };

        for (const auto &[bits, in_f, in_u, in_i, cast_f] : test_params) {
            Expr f_1 = in_f(x), f_2 = in_f(x + 16), f_3 = in_f(x + 32);
            Expr u_1 = in_u(x);
            Expr i_1 = in_i(x);

            // Arithmetic which could throw FP exception could return NaN, which results in output mismatch.
            // To avoid that, we need a positive value within certain range
            Func in_f_clamped;
            in_f_clamped(x) = clamp(in_f(x), cast_f(1e-3f), cast_f(1.0f));
            in_f_clamped.compute_root();  // To prevent LLVM optimization which results in a different instruction
            Expr f_1_clamped = in_f_clamped(x);
            Expr f_2_clamped = in_f_clamped(x + 16);

            if (bits == 16 && !is_float16_supported()) {
                continue;
            }

            vector total_bits_params = {256};  // {64, 128, 192, 256};
            if (bits != 64) {
                // Add scalar case to verify float16 native operation
                total_bits_params.push_back(bits);
            }

            for (auto total_bits : total_bits_params) {
                const int vf = total_bits / bits;
                const bool is_vector = vf > 1;

                const int instr_lanes = Instruction::get_instr_lanes(bits, vf, target);
                const int force_vectorized_lanes = Instruction::get_force_vectorized_instr_lanes(bits, vf, target);

                AddTestFunctor add(*this, bits, instr_lanes, vf);
                AddTestFunctor add_arm32_f32(*this, bits, vf, is_arm32() && bits == 32);
                AddTestFunctor add_arm64(*this, bits, instr_lanes, vf, !is_arm32());

                add({{sel_op("vabs.f", "fabs"), bits, force_vectorized_lanes}}, vf, abs(f_1));
                add(sel_op("vadd.f", "fadd"), f_1 + f_2);
                add(sel_op("vsub.f", "fsub"), f_1 - f_2);
                add(sel_op("vmul.f", "fmul"), f_1 * f_2);
                add("fdiv", sel_op("vdiv.f", "fdiv", "(fdiv|fdivr)"), f_1 / f_2_clamped);
                auto fneg_lanes = has_sve() ? force_vectorized_lanes : instr_lanes;
                add({{sel_op("vneg.f", "fneg"), bits, fneg_lanes}}, vf, -f_1);
                add({{sel_op("vsqrt.f", "fsqrt"), bits, force_vectorized_lanes}}, vf, sqrt(f_1_clamped));

                add_arm32_f32(is_vector ? "vceq.f" : "vcmp.f", select(f_1 == f_2, cast_f(1.0f), cast_f(2.0f)));
                add_arm32_f32(is_vector ? "vcgt.f" : "vcmp.f", select(f_1 > f_2, cast_f(1.0f), cast_f(2.0f)));
                add_arm64(is_vector ? "fcmeq" : "fcmp", select(f_1 == f_2, cast_f(1.0f), cast_f(2.0f)));
                add_arm64(is_vector ? "fcmgt" : "fcmp", select(f_1 > f_2, cast_f(1.0f), cast_f(2.0f)));

                add_arm32_f32("vcvt.f32.u", cast_f(u_1));
                add_arm32_f32("vcvt.f32.s", cast_f(i_1));
                add_arm32_f32("vcvt.u32.f", cast(UInt(bits), f_1));
                add_arm32_f32("vcvt.s32.f", cast(Int(bits), f_1));
                // The max of Float(16) is less than that of UInt(16), which generates "nan" in emulator
                Expr float_max = Float(bits).max();
                add_arm64("ucvtf", cast_f(min(float_max, u_1)));
                add_arm64("scvtf", cast_f(i_1));
                add_arm64({{"fcvtzu", bits, force_vectorized_lanes}}, vf, cast(UInt(bits), f_1));
                add_arm64({{"fcvtzs", bits, force_vectorized_lanes}}, vf, cast(Int(bits), f_1));
                add_arm64({{"frintn", bits, force_vectorized_lanes}}, vf, round(f_1));
                add_arm64({{"frintm", bits, force_vectorized_lanes}}, vf, floor(f_1));
                add_arm64({{"frintp", bits, force_vectorized_lanes}}, vf, ceil(f_1));
                add_arm64({{"frintz", bits, force_vectorized_lanes}}, vf, trunc(f_1));

                add_arm32_f32({{"vmax.f", bits, force_vectorized_lanes}}, vf, max(f_1, f_2));
                add_arm32_f32({{"vmin.f", bits, force_vectorized_lanes}}, vf, min(f_1, f_2));

                add_arm64({{"fmax", bits, force_vectorized_lanes}}, vf, max(f_1, f_2));
                add_arm64({{"fmin", bits, force_vectorized_lanes}}, vf, min(f_1, f_2));
                if (bits != 64 && total_bits != 192) {
                    // Halide relies on LLVM optimization for this pattern, and in some case it doesn't work
                    add_arm64("fmla", is_vector ? (has_sve() ? "(fmla|fmad)" : "fmla") : "fmadd", f_1 + f_2 * f_3);
                    add_arm64("fmls", is_vector ? (has_sve() ? "(fmls|fmsb)" : "fmls") : "fmsub", f_1 - f_2 * f_3);
                }
                if (bits != 64) {
                    add_arm64(vector<string>{"frecpe", "frecps"}, fast_inverse(f_1_clamped));
                    add_arm64(vector<string>{"frsqrte", "frsqrts"}, fast_inverse_sqrt(f_1_clamped));
                }

                if (bits == 16) {
                    // Some of the math ops (exp,log,pow) for fp16 are converted into "xxx_fp32" call
                    // and then lowered to Internal::halide_xxx() function.
                    // In case the target has FP16 feature, native type conversion between fp16 and fp32 should be generated
                    // instead of emulated equivalent code with other types.
                    if (is_vector && !has_sve()) {
                        add_arm64("exp", {{"fcvtl", 16, 4}, {"fcvtn", 16, 4}}, vf, exp(f_1_clamped));
                        add_arm64("log", {{"fcvtl", 16, 4}, {"fcvtn", 16, 4}}, vf, log(f_1_clamped));
                        add_arm64("pow", {{"fcvtl", 16, 4}, {"fcvtn", 16, 4}}, vf, pow(f_1_clamped, f_2_clamped));
                    } else {
                        add_arm64("exp", "fcvt", exp(f_1_clamped));
                        add_arm64("log", "fcvt", log(f_1_clamped));
                        add_arm64("pow", "fcvt", pow(f_1_clamped, f_2_clamped));
                    }
                }

                // No corresponding instructions exists for is_nan, is_inf, is_finite.
                // The instructions expected to be generated depends on CodeGen_LLVM::visit(const Call *op)
                add_arm64("nan", is_vector ? sel_op("", "fcmge", "fcmuo") : "fcmp", is_nan(f_1));
                add_arm64("inf", is_vector ? sel_op("", "fcmge", "fcmeq") : "", is_inf(f_1));
                add_arm64("finite", is_vector ? sel_op("", "fcmge", "fcmeq") : "", is_inf(f_1));
            }

            if (bits == 16) {
                // Actually, the following ops are not vectorized because SIMD instruction is unavailable.
                // The purpose of the test is just to confirm no error.
                // In case the target has FP16 feature, native type conversion between fp16 and fp32 should be generated
                // instead of emulated equivalent code with other types.
                AddTestFunctor add_f16(*this, 16, 1);

                add_f16("sinf", {{"bl", "sinf"}, {"fcvt", 16, 1}}, 1, sin(f_1_clamped));
                add_f16("asinf", {{"bl", "asinf"}, {"fcvt", 16, 1}}, 1, asin(f_1_clamped));
                add_f16("cosf", {{"bl", "cosf"}, {"fcvt", 16, 1}}, 1, cos(f_1_clamped));
                add_f16("acosf", {{"bl", "acosf"}, {"fcvt", 16, 1}}, 1, acos(f_1_clamped));
                add_f16("tanf", {{"bl", "tanf"}, {"fcvt", 16, 1}}, 1, tan(f_1_clamped));
                add_f16("atanf", {{"bl", "atanf"}, {"fcvt", 16, 1}}, 1, atan(f_1_clamped));
                add_f16("atan2f", {{"bl", "atan2f"}, {"fcvt", 16, 1}}, 1, atan2(f_1_clamped, f_2_clamped));
                add_f16("sinhf", {{"bl", "sinhf"}, {"fcvt", 16, 1}}, 1, sinh(f_1_clamped));
                add_f16("asinhf", {{"bl", "asinhf"}, {"fcvt", 16, 1}}, 1, asinh(f_1_clamped));
                add_f16("coshf", {{"bl", "coshf"}, {"fcvt", 16, 1}}, 1, cosh(f_1_clamped));
                add_f16("acoshf", {{"bl", "acoshf"}, {"fcvt", 16, 1}}, 1, acosh(max(f_1, cast_f(1.0f))));
                add_f16("tanhf", {{"bl", "tanhf"}, {"fcvt", 16, 1}}, 1, tanh(f_1_clamped));
                add_f16("atanhf", {{"bl", "atanhf"}, {"fcvt", 16, 1}}, 1, atanh(clamp(f_1, cast_f(-0.5f), cast_f(0.5f))));
            }
        }
    }

    void check_arm_load_store() {
        vector<tuple<Type, CastFuncTy>> test_params = {
            {Int(8), in_i8}, {Int(16), in_i16}, {Int(32), in_i32}, {Int(64), in_i64}, {UInt(8), in_u8}, {UInt(16), in_u16}, {UInt(32), in_u32}, {UInt(64), in_u64}, {Float(16), in_f16}, {Float(32), in_f32}, {Float(64), in_f64}};

        for (const auto &[elt, in_im] : test_params) {
            const int bits = elt.bits();
            if ((elt == Float(16) && !is_float16_supported()) ||
                (is_arm32() && bits == 64)) {
                continue;
            }

            // LD/ST       -       Load/Store
            for (int width = 64; width <= 64 * 4; width *= 2) {
                const int total_lanes = width / bits;
                const int instr_lanes = min(total_lanes, 128 / bits);
                if (instr_lanes < 2) continue;  // bail out scalar op

                // In case of arm32, instruction selection looks inconsistent due to optimization by LLVM
                AddTestFunctor add(*this, bits, total_lanes, target.bits == 64);
                // NOTE: if the expr is too simple, LLVM might generate "bl memcpy"
                Expr load_store_1 = in_im(x) * 3;

                if (has_sve()) {
                    // This pattern has changed with LLVM 21, see https://github.com/halide/Halide/issues/8584 for more
                    // details.
                    if (Halide::Internal::get_llvm_version() < 210) {
                        // in native width, ld1b/st1b is used regardless of data type
                        const bool allow_byte_ls = (width == target.vector_bits);
                        add({get_sve_ls_instr("ld1", bits, bits, "", allow_byte_ls ? "b" : "")}, total_lanes, load_store_1);
                        add({get_sve_ls_instr("st1", bits, bits, "", allow_byte_ls ? "b" : "")}, total_lanes, load_store_1);
                    }
                } else {
                    // vector register is not used for simple load/store
                    string reg_prefix = (width <= 64) ? "d" : "q";
                    add({{"st[rp]", reg_prefix + R"(\d\d?)"}}, total_lanes, load_store_1);
                    add({{"ld[rp]", reg_prefix + R"(\d\d?)"}}, total_lanes, load_store_1);
                }
            }

            // LD2/ST2       -       Load/Store two-element structures
            int base_vec_bits = has_sve() ? target.vector_bits : 128;
            for (int width = base_vec_bits; width <= base_vec_bits * 4; width *= 2) {
                const int total_lanes = width / bits;
                const int vector_lanes = total_lanes / 2;
                const int instr_lanes = min(vector_lanes, base_vec_bits / bits);
                if (instr_lanes < 2) continue;  // bail out scalar op

                AddTestFunctor add_ldn(*this, bits, vector_lanes);
                AddTestFunctor add_stn(*this, bits, instr_lanes, total_lanes);

                Func tmp1, tmp2;
                tmp1(x) = cast(elt, x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 2 == 0, tmp1(x / 2), tmp1(x / 2 + 16));
                tmp2.compute_root().vectorize(x, total_lanes);
                Expr load_2 = in_im(x * 2) + in_im(x * 2 + 1);
                Expr store_2 = tmp2(0, 0) + tmp2(0, 127);

                if (has_sve()) {
                    // TODO(inssue needed): Added strided load support.
#if 0
                    add_ldn({get_sve_ls_instr("ld2", bits)}, vector_lanes, load_2);
#endif
                    add_stn({get_sve_ls_instr("st2", bits)}, total_lanes, store_2);
                } else {
                    add_ldn(sel_op("vld2.", "ld2"), load_2);
                    add_stn(sel_op("vst2.", "st2"), store_2);
                }
            }

            // Also check when the two expressions interleaved have a common
            // subexpression, which results in a vector var being lifted out.
            for (int width = base_vec_bits; width <= base_vec_bits * 4; width *= 2) {
                const int total_lanes = width / bits;
                const int vector_lanes = total_lanes / 2;
                const int instr_lanes = Instruction::get_instr_lanes(bits, vector_lanes, target);
                if (instr_lanes < 2) continue;  // bail out scalar op

                AddTestFunctor add_stn(*this, bits, instr_lanes, total_lanes);

                Func tmp1, tmp2;
                tmp1(x) = cast(elt, x);
                tmp1.compute_root();
                Expr e = (tmp1(x / 2) * 2 + 7) / 4;
                tmp2(x, y) = select(x % 2 == 0, e * 3, e + 17);
                tmp2.compute_root().vectorize(x, total_lanes);
                Expr store_2 = tmp2(0, 0) + tmp2(0, 127);

                if (has_sve()) {
                    add_stn({get_sve_ls_instr("st2", bits)}, total_lanes, store_2);
                } else {
                    add_stn(sel_op("vst2.", "st2"), store_2);
                }
            }

            // LD3/ST3       -       Store three-element structures
            for (int width = 192; width <= 192 * 4; width *= 2) {
                const int total_lanes = width / bits;
                const int vector_lanes = total_lanes / 3;
                const int instr_lanes = Instruction::get_instr_lanes(bits, vector_lanes, target);
                if (instr_lanes < 2) continue;  // bail out scalar op

                AddTestFunctor add_ldn(*this, bits, vector_lanes);
                AddTestFunctor add_stn(*this, bits, instr_lanes, total_lanes);

                Func tmp1, tmp2;
                tmp1(x) = cast(elt, x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 3 == 0, tmp1(x / 3),
                                    x % 3 == 1, tmp1(x / 3 + 16),
                                    tmp1(x / 3 + 32));
                tmp2.compute_root().vectorize(x, total_lanes);
                Expr load_3 = in_im(x * 3) + in_im(x * 3 + 1) + in_im(x * 3 + 2);
                Expr store_3 = tmp2(0, 0) + tmp2(0, 127);

                if (has_sve()) {
                    // TODO(issue needed): Added strided load support.
#if 0
                    add_ldn({get_sve_ls_instr("ld3", bits)}, vector_lanes, load_3);
                    add_stn({get_sve_ls_instr("st3", bits)}, total_lanes, store_3);
#endif
                } else {
                    add_ldn(sel_op("vld3.", "ld3"), load_3);
                    add_stn(sel_op("vst3.", "st3"), store_3);
                }
            }

            // LD4/ST4       -       Store four-element structures
            for (int width = 256; width <= 256 * 4; width *= 2) {
                const int total_lanes = width / bits;
                const int vector_lanes = total_lanes / 4;
                const int instr_lanes = Instruction::get_instr_lanes(bits, vector_lanes, target);
                if (instr_lanes < 2) continue;  // bail out scalar op

                AddTestFunctor add_ldn(*this, bits, vector_lanes);
                AddTestFunctor add_stn(*this, bits, instr_lanes, total_lanes);

                Func tmp1, tmp2;
                tmp1(x) = cast(elt, x);
                tmp1.compute_root();
                tmp2(x, y) = select(x % 4 == 0, tmp1(x / 4),
                                    x % 4 == 1, tmp1(x / 4 + 16),
                                    x % 4 == 2, tmp1(x / 4 + 32),
                                    tmp1(x / 4 + 48));
                tmp2.compute_root().vectorize(x, total_lanes);
                Expr load_4 = in_im(x * 4) + in_im(x * 4 + 1) + in_im(x * 4 + 2) + in_im(x * 4 + 3);
                Expr store_4 = tmp2(0, 0) + tmp2(0, 127);

                if (has_sve()) {
                    // TODO(issue needed): Added strided load support.
#if 0
                    add_ldn({get_sve_ls_instr("ld4", bits)}, vector_lanes, load_4);
                    add_stn({get_sve_ls_instr("st4", bits)}, total_lanes, store_4);
#endif
                } else {
                    add_ldn(sel_op("vld4.", "ld4"), load_4);
                    add_stn(sel_op("vst4.", "st4"), store_4);
                }
            }

            // SVE Gather/Scatter
            if (has_sve()) {
                for (int width = 64; width <= 64 * 4; width *= 2) {
                    const int total_lanes = width / bits;
                    const int instr_lanes = min(total_lanes, 128 / bits);
                    if (instr_lanes < 2) continue;  // bail out scalar op

                    AddTestFunctor add(*this, bits, total_lanes);
                    Expr index = clamp(cast<int>(in_im(x)), 0, W - 1);
                    Func tmp;
                    tmp(x, y) = cast(elt, y);
                    tmp(x, index) = cast(elt, 1);
                    tmp.compute_root().update().vectorize(x, total_lanes);
                    Expr gather = in_im(index);
                    Expr scatter = tmp(0, 0) + tmp(0, 127);

                    const int index_bits = std::max(32, bits);
                    add({get_sve_ls_instr("ld1", bits, index_bits, "uxtw")}, total_lanes, gather);
                    add({get_sve_ls_instr("st1", bits, index_bits, "uxtw")}, total_lanes, scatter);
                }
            }
        }
    }

    void check_arm_pairwise() {
        // A summation reduction that starts at something
        // non-trivial, to avoid llvm simplifying accumulating
        // widening summations into just widening summations.
        auto sum_ = [&](Expr e) {
            Func f;
            f(x) = cast(e.type(), 123);
            f(x) += e;
            return f(x);
        };

        // Tests for integer type
        {
            vector<tuple<int, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy, CastFuncTy>> test_params{
                {8, in_i8, in_u8, i16, i32, u16, u32},
                {16, in_i16, in_u16, i32, i64, u32, u64},
                {32, in_i32, in_u32, i64, i64, u64, u64},
                {64, in_i64, in_u64, i64, i64, u64, u64},
            };
            // clang-format on

            for (const auto &[bits, in_i, in_u, widen_i, widenx4_i, widen_u, widenx4_u] : test_params) {

                for (auto &total_bits : {64, 128}) {
                    const int vf = total_bits / bits;
                    const int instr_lanes = Instruction::get_force_vectorized_instr_lanes(bits, vf, target);
                    AddTestFunctor add(*this, bits, instr_lanes, vf, !(is_arm32() && bits == 64));  // 64 bit is unavailable in neon 32 bit
                    AddTestFunctor add_8_16_32(*this, bits, instr_lanes, vf, bits != 64);
                    const int widen_lanes = Instruction::get_instr_lanes(bits, vf * 2, target);
                    AddTestFunctor add_widen(*this, bits, widen_lanes, vf, bits != 64);

                    if (!has_sve()) {
                        // VPADD    I, F    -       Pairwise Add
                        // VPMAX    I, F    -       Pairwise Maximum
                        // VPMIN    I, F    -       Pairwise Minimum
                        for (int f : {2, 4}) {
                            RDom r(0, f);

                            add(sel_op("vpadd.i", "addp"), sum_(in_i(f * x + r)));
                            add(sel_op("vpadd.i", "addp"), sum_(in_u(f * x + r)));
                            add_8_16_32(sel_op("vpmax.s", "smaxp"), maximum(in_i(f * x + r)));
                            add_8_16_32(sel_op("vpmax.u", "umaxp"), maximum(in_u(f * x + r)));
                            add_8_16_32(sel_op("vpmin.s", "sminp"), minimum(in_i(f * x + r)));
                            add_8_16_32(sel_op("vpmin.u", "uminp"), minimum(in_u(f * x + r)));
                        }
                    }

                    // VPADAL   I       -       Pairwise Add and Accumulate Long
                    // VPADDL   I       -       Pairwise Add Long
                    {
                        int f = 2;
                        RDom r(0, f);

                        // If we're reducing by a factor of two, we can
                        // use the forms with an accumulator
                        add_widen(sel_op("vpadal.s", "sadalp"), sum_(widen_i(in_i(f * x + r))));
                        add_widen(sel_op("vpadal.u", "uadalp"), sum_(widen_i(in_u(f * x + r))));
                        add_widen(sel_op("vpadal.u", "uadalp"), sum_(widen_u(in_u(f * x + r))));
                    }
                    {
                        int f = 4;
                        RDom r(0, f);

                        // If we're reducing by more than that, that's not
                        // possible.
                        // In case of SVE, addlp is unavailable, so adalp is used with accumulator=0 instead.
                        add_widen(sel_op("vpaddl.s", "saddlp", "sadalp"), sum_(widen_i(in_i(f * x + r))));
                        add_widen(sel_op("vpaddl.u", "uaddlp", "uadalp"), sum_(widen_i(in_u(f * x + r))));
                        add_widen(sel_op("vpaddl.u", "uaddlp", "uadalp"), sum_(widen_u(in_u(f * x + r))));
                    }

                    const bool is_arm_dot_prod_available = (!is_arm32() && target.has_feature(Target::ARMDotProd) && bits == 8) ||
                                                           (has_sve() && (bits == 8 || bits == 16));
                    if ((bits == 8 || bits == 16) && !is_arm_dot_prod_available) {  // udot/sdot is applied if available
                        int f = 4;
                        RDom r(0, f);
                        // If we're widening the type by a factor of four
                        // as well as reducing by a factor of four, we
                        // expect vpaddl followed by vpadal
                        // Note that when going from u8 to i32 like this,
                        // the vpaddl is unsigned and the vpadal is a
                        // signed, because the intermediate type is u16
                        const int widenx4_lanes = Instruction::get_instr_lanes(bits * 2, vf, target);
                        string op_addl, op_adal;
                        op_addl = sel_op("vpaddl.s", "saddlp");
                        op_adal = sel_op("vpadal.s", "sadalp");
                        add({{op_addl, bits, widen_lanes}, {op_adal, bits * 2, widenx4_lanes}}, vf, sum_(widenx4_i(in_i(f * x + r))));
                        op_addl = sel_op("vpaddl.u", "uaddlp");
                        op_adal = sel_op("vpadal.u", "uadalp");
                        add({{op_addl, bits, widen_lanes}, {op_adal, bits * 2, widenx4_lanes}}, vf, sum_(widenx4_i(in_u(f * x + r))));
                        add({{op_addl, bits, widen_lanes}, {op_adal, bits * 2, widenx4_lanes}}, vf, sum_(widenx4_u(in_u(f * x + r))));
                    }

                    // UDOT/SDOT
                    if (is_arm_dot_prod_available) {
                        const int factor_32bit = vf / 4;
                        for (int f : {4, 8}) {
                            // checks vector register for narrow src data type (i.e. 8 or 16 bit)
                            const int lanes_src = Instruction::get_instr_lanes(bits, f * factor_32bit, target);
                            AddTestFunctor add_dot(*this, bits, lanes_src, factor_32bit);
                            RDom r(0, f);

                            add_dot("udot", sum(widenx4_u(in_u(f * x + r)) * in_u(f * x + r + 32)));
                            add_dot("sdot", sum(widenx4_i(in_i(f * x + r)) * in_i(f * x + r + 32)));
                            if (f == 4) {
                                // This doesn't generate for higher reduction factors because the
                                // intermediate is 16-bit instead of 32-bit. It seems like it would
                                // be slower to fix this (because the intermediate sum would be
                                // 32-bit instead of 16-bit).
                                add_dot("udot", sum(widenx4_u(in_u(f * x + r))));
                                add_dot("sdot", sum(widenx4_i(in_i(f * x + r))));
                            }
                        }
                    }
                }
            }
        }

        // Tests for Float type
        {
            // clang-format off
            vector<tuple<int, CastFuncTy>> test_params{
                {16, in_f16},
                {32, in_f32},
                {64, in_f64},
            };
            // clang-format on
            if (!has_sve()) {
                for (const auto &[bits, in_f] : test_params) {
                    for (auto &total_bits : {64, 128}) {
                        const int vf = total_bits / bits;
                        if (vf < 2) continue;
                        AddTestFunctor add(*this, bits, vf);
                        AddTestFunctor add_16_32(*this, bits, vf, bits != 64);

                        if (bits == 16 && !is_float16_supported()) {
                            continue;
                        }

                        for (int f : {2, 4}) {
                            RDom r(0, f);

                            add(sel_op("vadd.f", "faddp"), sum_(in_f(f * x + r)));
                            add_16_32(sel_op("vmax.f", "fmaxp"), maximum(in_f(f * x + r)));
                            add_16_32(sel_op("vmin.f", "fminp"), minimum(in_f(f * x + r)));
                        }
                    }
                }
            }
        }
    }

    struct ArmTask {
        vector<string> instrs;
    };

    struct Instruction {
        string opcode;
        optional<string> operand;
        optional<int> bits;
        optional<int> pattern_lanes;
        static inline const int ANY_LANES = -1;

        // matching pattern for opcode/operand is directly set
        Instruction(const string &opcode, const string &operand)
            : opcode(opcode), operand(operand), bits(nullopt), pattern_lanes(nullopt) {
        }

        // matching pattern for opcode/operand is generated from bits/lanes
        Instruction(const string &opcode, int bits, int lanes)
            : opcode(opcode), operand(nullopt), bits(bits), pattern_lanes(lanes) {
        }

        string generate_pattern(const Target &target) const {
            bool is_arm32 = target.bits == 32;
            bool has_sve = target.has_feature(Target::SVE2);

            string opcode_pattern;
            string operand_pattern;
            if (bits && pattern_lanes) {
                if (is_arm32) {
                    opcode_pattern = get_opcode_neon32();
                    operand_pattern = get_reg_neon32();
                } else if (!has_sve) {
                    opcode_pattern = opcode;
                    operand_pattern = get_reg_neon64();
                } else {
                    opcode_pattern = opcode;
                    operand_pattern = get_reg_sve();
                }
            } else {
                opcode_pattern = opcode;
                operand_pattern = operand.value_or("");
            }
            // e.g "add v15.h "  ->  "\s*add\s.*\bv\d\d?\.h\b.*"
            return opcode_pattern + R"(\s.*\b)" + operand_pattern + R"(\b.*)";
        }

        // TODO Fix this for SVE2
        static int natural_lanes(int bits) {
            return 128 / bits;
        }

        static int get_instr_lanes(int bits, int vec_factor, const Target &target) {
            return min(natural_lanes(bits), vec_factor);
        }

        static int get_force_vectorized_instr_lanes(int bits, int vec_factor, const Target &target) {
            // For some cases, where scalar operation is forced to vectorize
            if (target.has_feature(Target::SVE2)) {
                if (vec_factor == 1) {
                    return 1;
                } else {
                    return natural_lanes(bits);
                }
            } else {
                int min_lanes = std::max(2, natural_lanes(bits) / 2);  // 64 bit wide VL
                return max(min_lanes, get_instr_lanes(bits, vec_factor, target));
            }
        }

        string get_opcode_neon32() const {
            return opcode + to_string(bits.value());
        }

        const char *get_bits_designator() const {
            static const map<int, const char *> designators{
                // NOTE: vector or float only
                {8, "b"},
                {16, "h"},
                {32, "s"},
                {64, "d"},
            };
            auto iter = designators.find(bits.value());
            assert(iter != designators.end());
            return iter->second;
        }

        string get_reg_sve() const {
            if (pattern_lanes == ANY_LANES) {
                return R"((z\d\d?\.[bhsd])|(s\d\d?))";
            } else {
                const char *bits_designator = get_bits_designator();
                // TODO(need issue): This should only match the scalar register, and likely a NEON instruction opcode.
                // Generating a full SVE vector instruction for a scalar operation is inefficient. However this is
                // happening and fixing it involves changing intrinsic selection. Likely to use NEON intrinsics where
                // applicable. For now, accept both a scalar operation and a vector one.
                std::string scalar_reg_pattern = (pattern_lanes > 1) ? "" : std::string("|(") + bits_designator + R"(\d\d?))";  // e.g. "h15"

                return std::string(R"(((z\d\d?\.)") + bits_designator + ")|(" +
                       R"(v\d\d?\.)" + to_string(pattern_lanes.value()) + bits_designator + ")" + scalar_reg_pattern + ")";
            }
        }

        string get_reg_neon32() const {
            return "";
        }

        string get_reg_neon64() const {
            const char *bits_designator = get_bits_designator();
            if (pattern_lanes == 1) {
                return std::string(bits_designator) + R"(\d\d?)";  // e.g. "h15"
            } else if (pattern_lanes == ANY_LANES) {
                return R"(v\d\d?\.[bhsd])";
            } else {
                return R"(v\d\d?\.)" + to_string(pattern_lanes.value()) + bits_designator;  // e.g. "v15.4h"
            }
        }
    };

    Instruction get_sve_ls_instr(const string &base_opcode, int opcode_bits, int operand_bits, const string &additional = "", const string &optional_type = "") {
        static const map<int, string> opcode_suffix_map = {{8, "b"}, {16, "h"}, {32, "w"}, {64, "d"}};
        static const map<int, string> operand_suffix_map = {{8, "b"}, {16, "h"}, {32, "s"}, {64, "d"}};
        string opcode_size_specifier;
        string operand_size_specifier;
        if (!optional_type.empty()) {
            opcode_size_specifier = "[";
            operand_size_specifier = "[";
        }
        opcode_size_specifier += opcode_suffix_map.at(opcode_bits);
        operand_size_specifier += operand_suffix_map.at(operand_bits);
        if (!optional_type.empty()) {
            opcode_size_specifier += optional_type;
            opcode_size_specifier += "]";
            operand_size_specifier += optional_type;
            operand_size_specifier += "]";
        }
        const string opcode = base_opcode + opcode_size_specifier;
        string operand = R"(z\d\d?\.)" + operand_size_specifier;
        if (!additional.empty()) {
            operand += ", " + additional;
        }
        return Instruction(opcode, operand);
    }

    Instruction get_sve_ls_instr(const string &base_opcode, int bits) {
        return get_sve_ls_instr(base_opcode, bits, bits, "");
    }

    // Helper functor to add test case
    class AddTestFunctor {
    public:
        AddTestFunctor(SimdOpCheckArmSve &p,
                       int default_bits,
                       int default_instr_lanes,
                       int default_vec_factor,
                       bool is_enabled = true /* false to skip testing */)
            : parent(p), default_bits(default_bits), default_instr_lanes(default_instr_lanes),
              default_vec_factor(default_vec_factor), is_enabled(is_enabled) {};

        AddTestFunctor(SimdOpCheckArmSve &p,
                       int default_bits,
                       // default_instr_lanes is inferred from bits and vec_factor
                       int default_vec_factor,
                       bool is_enabled = true /* false to skip testing */)
            : parent(p), default_bits(default_bits),
              default_instr_lanes(Instruction::get_instr_lanes(default_bits, default_vec_factor, p.target)),
              default_vec_factor(default_vec_factor), is_enabled(is_enabled) {};

        // Constructs single Instruction with default parameters
        void operator()(const string &opcode, Expr e) {
            // Use opcode for name
            (*this)(opcode, opcode, e);
        }

        // Constructs single Instruction with default parameters except for custom name
        void operator()(const string &op_name, const string &opcode, Expr e) {
            create_and_register(op_name, {Instruction{opcode, default_bits, default_instr_lanes}}, default_vec_factor, e);
        }

        // Constructs multiple Instruction with default parameters
        void operator()(const vector<string> &opcodes, Expr e) {
            assert(!opcodes.empty());
            (*this)(opcodes[0], opcodes, e);
        }

        // Constructs multiple Instruction with default parameters except for custom name
        void operator()(const string &op_name, const vector<string> &opcodes, Expr e) {
            vector<Instruction> instrs;
            for (const auto &opcode : opcodes) {
                instrs.emplace_back(opcode, default_bits, default_instr_lanes);
            }
            create_and_register(op_name, instrs, default_vec_factor, e);
        }

        // Set single or multiple Instructions of custom parameters
        void operator()(const vector<Instruction> &instructions, int vec_factor, Expr e) {
            // Use the 1st opcode for name
            assert(!instructions.empty());
            string op_name = instructions[0].opcode;
            (*this)(op_name, instructions, vec_factor, e);
        }

        // Set single or multiple Instructions of custom parameters, with custom name
        void operator()(const string &op_name, const vector<Instruction> &instructions, int vec_factor, Expr e) {
            create_and_register(op_name, instructions, vec_factor, e);
        }

    private:
        void create_and_register(const string &op_name, const vector<Instruction> &instructions, int vec_factor, Expr e) {
            if (!is_enabled) return;

            // Generate regular expression for the instruction we check
            vector<string> instr_patterns;
            transform(instructions.begin(), instructions.end(), back_inserter(instr_patterns),
                      [t = parent.target](const Instruction &instr) { return instr.generate_pattern(t); });

            std::stringstream type_name_stream;
            type_name_stream << e.type();
            std::string decorated_op_name = op_name + "_" + type_name_stream.str() + "_x" + std::to_string(vec_factor);
            auto unique_name = "op_" + decorated_op_name + "_" + std::to_string(parent.tasks.size());

            // Bail out after generating the unique_name, so that names are
            // unique across different processes and don't depend on filter
            // settings.
            if (!parent.wildcard_match(parent.filter, decorated_op_name)) return;

            // Create a deep copy of the expr and all Funcs referenced by it, so
            // that no IR is shared between tests. This is required by the base
            // class, and is why we can parallelize.
            {
                using namespace Halide::Internal;
                class FindOutputs : public IRVisitor {
                    using IRVisitor::visit;
                    void visit(const Call *op) override {
                        if (op->func.defined()) {
                            outputs.insert(op->func);
                        }
                        IRVisitor::visit(op);
                    }

                public:
                    std::set<FunctionPtr> outputs;
                } finder;
                e.accept(&finder);
                std::vector<Function> outputs(finder.outputs.begin(), finder.outputs.end());
                auto env = deep_copy(outputs, build_environment(outputs)).second;
                class DeepCopy : public IRMutator {
                    std::map<FunctionPtr, FunctionPtr> copied;
                    using IRMutator::visit;
                    Expr visit(const Call *op) override {
                        if (op->func.defined()) {
                            auto it = env.find(op->name);
                            if (it != env.end()) {
                                return Func(it->second)(mutate(op->args));
                            }
                        }
                        return IRMutator::visit(op);
                    }
                    const std::map<std::string, Function> &env;

                public:
                    DeepCopy(const std::map<std::string, Function> &env)
                        : env(env) {
                    }
                } copier(env);
                e = copier.mutate(e);
            }

            // Create Task and register
            parent.tasks.emplace_back(Task{decorated_op_name, unique_name, vec_factor, e});
            parent.arm_tasks.emplace(unique_name, ArmTask{std::move(instr_patterns)});
        }

        SimdOpCheckArmSve &parent;
        int default_bits;
        int default_instr_lanes;
        int default_vec_factor;
        bool is_enabled;
    };

    void compile_and_check(Func error, const string &op, const string &name, int vector_width, const std::vector<Argument> &arg_types, ostringstream &error_msg) override {
        // This is necessary as LLVM validation errors, crashes, etc. don't tell which op crashed.
        cout << "Starting op " << op << "\n";
        string fn_name = "test_" + name;
        string file_name = output_directory + fn_name;

        auto ext = Internal::get_output_info(target);
        std::map<OutputFileType, std::string> outputs = {
            {OutputFileType::llvm_assembly, file_name + ext.at(OutputFileType::llvm_assembly).extension},
            {OutputFileType::c_header, file_name + ext.at(OutputFileType::c_header).extension},
            {OutputFileType::object, file_name + ext.at(OutputFileType::object).extension},
            {OutputFileType::assembly, file_name + ".s"},
        };

        error.compile_to(outputs, arg_types, fn_name, target);

        std::ifstream asm_file;
        asm_file.open(file_name + ".s");

        auto arm_task = arm_tasks.find(name);
        assert(arm_task != arm_tasks.end());

        std::ostringstream msg;
        msg << op << " did not generate for target=" << target.to_string()
            << " vector_width=" << vector_width << ". Instead we got:\n";

        string line;
        vector<string> matched_lines;
        vector<string> &patterns = arm_task->second.instrs;
        while (getline(asm_file, line) && !patterns.empty()) {
            msg << line << "\n";
            auto pattern = patterns.begin();
            while (pattern != patterns.end()) {
                smatch match;
                if (regex_search(line, match, regex(*pattern))) {
                    pattern = patterns.erase(pattern);
                    matched_lines.emplace_back(match[0]);
                } else {
                    ++pattern;
                }
            }
        }

        if (!patterns.empty()) {
            error_msg << "Failed: " << msg.str() << "\n";
            error_msg << "The following instruction patterns were not found:\n";
            for (auto &p : patterns) {
                error_msg << p << "\n";
            }
        } else if (debug_mode == "1") {
            for (auto &l : matched_lines) {
                error_msg << "    " << setw(20) << name << ", vf=" << setw(2) << vector_width << ",     ";
                error_msg << l << endl;
            }
        }
    }

    inline const string &sel_op(const string &neon32, const string &neon64) {
        return is_arm32() ? neon32 : neon64;
    }

    inline const string &sel_op(const string &neon32, const string &neon64, const string &sve) {
        return is_arm32()                                                          ? neon32 :
               target.has_feature(Target::SVE) || target.has_feature(Target::SVE2) ? sve :
                                                                                     neon64;
    }

    inline bool is_arm32() const {
        return target.bits == 32;
    };
    inline bool has_neon() const {
        return !target.has_feature(Target::NoNEON);
    };
    inline bool has_sve() const {
        return target.has_feature(Target::SVE2);
    };

    bool is_float16_supported() const {
        return (target.bits == 64) && target.has_feature(Target::ARMFp16);
    }

    bool can_run_the_code;
    string debug_mode;
    std::unordered_map<string, ArmTask> arm_tasks;
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckArmSve>(
        argc, argv,
        {
            // IMPORTANT:
            // When adding new targets here, make sure to also update
            // can_run_code in simd_op_check.h to include any new features used.

            Target("arm-64-linux-sve2-no_neon-vector_bits_128"),
            Target("arm-64-linux-sve2-no_neon-vector_bits_256"),
        });
}

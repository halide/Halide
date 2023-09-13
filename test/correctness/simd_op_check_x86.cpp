#include "simd_op_check.h"

#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

// This tests that we can correctly generate all the simd ops for x86 targets.

class SimdOpCheckX86 : public SimdOpCheckTest {
public:
    SimdOpCheckX86(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
        // We only test the skylake variant of avx512 here
        use_avx512 = (target.has_feature(Target::AVX512_Cannonlake) ||
                      target.has_feature(Target::AVX512_Skylake));
        if (target.has_feature(Target::AVX512) && !use_avx512) {
            std::cerr << "Warning: This test is only configured for the skylake variant of avx512. Expect failures\n";
        }
        use_avx2 = use_avx512 || (target.has_feature(Target::AVX512) || target.has_feature(Target::AVX2));
        use_avx = use_avx2 || target.has_feature(Target::AVX);
        use_sse41 = use_avx || target.has_feature(Target::SSE41);

        // There's no separate target for SSSE3; we currently enable it in
        // lockstep with SSE4.1
        use_ssse3 = use_sse41;
        // There's no separate target for SSS4.2; we currently assume that
        // it should be used iff AVX is being used.
        use_sse42 = use_avx;

        use_avx512_vnni = target.has_feature(Target::AVX512_Zen4);
        use_avx_vnni = target.has_feature(Target::AVX512_SapphireRapids);
    }

    void add_tests() override {
        // Queue up a bunch of tasks representing each test to run.
        if (target.arch == Target::X86) {
            check_sse_and_avx();
        }
    }

    void check_sse_and_avx() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f16_1 = in_f16(x), f16_2 = in_f16(x + 16), f16_3 = in_f16(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // MMX and SSE1 (in 64 and 128 bits)
        for (int w = 1; w <= 4; w++) {
            // LLVM promotes these to wider types for 64-bit vectors,
            // which is probably fine. Often you're 64-bits wide because
            // you're about to upcast, and using the wider types makes the
            // upcast cheap.
            if (w > 1) {
                check("paddb", 8 * w, u8_1 + u8_2);
                check("psubb", 8 * w, u8_1 - u8_2);
                check("paddw", 4 * w, u16_1 + u16_2);
                check("psubw", 4 * w, u16_1 - u16_2);
                check("pmullw", 4 * w, i16_1 * i16_2);
                check("paddd", 2 * w, i32_1 + i32_2);
                check("psubd", 2 * w, i32_1 - i32_2);
            }

            check("paddsb", 8 * w, i8_sat(i16(i8_1) + i16(i8_2)));
            // Add a test with a constant as there was a bug on this.
            check("paddsb", 8 * w, i8_sat(i16(i8_1) + i16(3)));

            check("psubsb", 8 * w, i8_sat(i16(i8_1) - i16(i8_2)));

            check("paddusb", 8 * w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("psubusb", 8 * w, u8(max(i16(u8_1) - i16(u8_2), 0)));
            check("paddsw", 4 * w, i16_sat(i32(i16_1) + i32(i16_2)));
            check("psubsw", 4 * w, i16_sat(i32(i16_1) - i32(i16_2)));
            check("paddusw", 4 * w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("psubusw", 4 * w, u16(max(i32(u16_1) - i32(u16_2), 0)));

            // unsigned absd is lowered as an or of saturating subtracts
            check("psubusb", 16 * w, absd(u8_1, u8_2));
            check("psubusw", 16 * w, absd(u16_1, u16_2));

            check("pmulhw", 4 * w, i16((i32(i16_1) * i32(i16_2)) / (256 * 256)));
            check("pmulhw", 4 * w, i16((i32(i16_1) * i32(i16_2)) >> cast<unsigned>(16)));
            check("pmulhw", 4 * w, i16((i32(i16_1) * i32(i16_2)) >> cast<int>(16)));
            check("pmulhw", 4 * w, i16((i32(i16_1) * i32(i16_2)) << cast<int>(-16)));

            // Add a test with a constant as there was a bug on this.
            check("pmulhw", 4 * w, i16((3 * i32(i16_2)) / (256 * 256)));

            // There was a bug with this case too. CSE was lifting out the
            // information that made it possible to do the narrowing.
            check("pmulhw", 4 * w, select(in_u8(0) == 0, i16((3 * i32(i16_2)) / (256 * 256)), i16((5 * i32(i16_2)) / (256 * 256))));

            check("pmulhuw", 4 * w, i16_1 / 15);

            // Shifts by amounts other than 16 can also use this instruction, by
            // preshifting an arg (when there are bits of headroom), or
            // postshifting the result.
            check("pmulhuw", 4 * w, u16((u32(u16_1) * u32(u8_2)) >> 13));
            check("pmulhw", 4 * w, i16((i32(i16_1) * i32(i16_2)) >> 17));
            check("pmulhuw", 4 * w, u16((u32(u16_1) * u32(u16_2)) >> 18));

            if (w > 1) {  // LLVM does a lousy job at the comparisons for 64-bit types
                check("pcmp*b", 8 * w, select(u8_1 == u8_2, u8(1), u8(2)));
                check("pcmp*b", 8 * w, select(u8_1 > u8_2, u8(1), u8(2)));
                check("pcmp*w", 4 * w, select(u16_1 == u16_2, u16(1), u16(2)));
                check("pcmp*w", 4 * w, select(u16_1 > u16_2, u16(1), u16(2)));
                check("pcmp*d", 2 * w, select(u32_1 == u32_2, u32(1), u32(2)));
                check("pcmp*d", 2 * w, select(u32_1 > u32_2, u32(1), u32(2)));
            }

            // SSE 1
            check("addps", 2 * w, f32_1 + f32_2);
            check("subps", 2 * w, f32_1 - f32_2);
            check("mulps", 2 * w, f32_1 * f32_2);

            // Padding out the lanes of a div isn't necessarily a good
            // idea, and so llvm doesn't do it.
            if (w > 1) {
                // LLVM no longer generates division instructions with
                // fast-math on (instead it uses the approximate
                // reciprocal, a newtown rhapson step, and a
                // multiplication by the numerator).
                // check("divps", 2*w, f32_1 / f32_2);
            }

            check(use_avx512 ? "vrsqrt*ps" : "rsqrtps", 2 * w, fast_inverse_sqrt(f32_1));
            check(use_avx512 ? "vrcp*ps" : "rcpps", 2 * w, fast_inverse(f32_1));
            check("sqrtps", 2 * w, sqrt(f32_2));
            check("maxps", 2 * w, max(f32_1, f32_2));
            check("minps", 2 * w, min(f32_1, f32_2));
            check("pavgb", 8 * w, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
            check("pavgb", 8 * w, u8((u16(u8_1) + u16(u8_2) + 1) >> 1));
            check("pavgw", 4 * w, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
            check("pavgw", 4 * w, u16((u32(u16_1) + u32(u16_2) + 1) >> 1));

            // Rounding right shifts, halving subtracts, and signed rounding
            // averages should also use pavg
            check("pavgb", 8 * w, rounding_shift_right(u8_1, 2));
            check("pavgw", 4 * w, rounding_shift_right(u16_1, 2));
            check("pavgb", 8 * w, halving_sub(u8_1, u8_2));
            check("pavgw", 4 * w, halving_sub(u16_1, u16_2));
            check("pavgb", 8 * w, rounding_halving_add(i8_1, i8_2));
            check("pavgw", 4 * w, rounding_halving_add(i16_1, i16_2));

            check("pmaxsw", 4 * w, max(i16_1, i16_2));
            check("pminsw", 4 * w, min(i16_1, i16_2));
            check("pmaxub", 8 * w, max(u8_1, u8_2));
            check("pminub", 8 * w, min(u8_1, u8_2));

            const char *check_pmulhuw = (use_avx2 && w > 3) ? "vpmulhuw*ymm" : "pmulhuw";
            check(check_pmulhuw, 4 * w, u16((u32(u16_1) * u32(u16_2)) / (256 * 256)));
            check(check_pmulhuw, 4 * w, u16((u32(u16_1) * u32(u16_2)) >> cast<unsigned>(16)));
            check(check_pmulhuw, 4 * w, u16((u32(u16_1) * u32(u16_2)) >> cast<int>(16)));
            check(check_pmulhuw, 4 * w, u16((u32(u16_1) * u32(u16_2)) << cast<int>(-16)));
            check(check_pmulhuw, 4 * w, u16_1 / 15);

            check("cmpeqps", 2 * w, select(f32_1 == f32_2, 1.0f, 2.0f));
            check("cmpltps", 2 * w, select(f32_1 < f32_2, 1.0f, 2.0f));

            // These get normalized to not of eq, and not of lt with the args flipped
            // check("cmpneqps", 2*w, cast<int32_t>(f32_1 != f32_2));
            // check("cmpleps", 2*w, cast<int32_t>(f32_1 <= f32_2));
        }

        // These guys get normalized to the integer versions for widths
        // other than 128-bits. Avx512 has mask-register versions.
        // check("andnps", 4, bool_1 & (~bool_2));
        check(use_avx512 ? "korw" : "orps", 4, bool_1 | bool_2);
        check(use_avx512 ? "kxorw" : "xorps", 4, bool_1 ^ bool_2);
        if (!use_avx512) {
            // avx512 implicitly ands the predicates by masking the second
            // comparison using the result of the first. Clever!
            check("andps", 4, bool_1 & bool_2);
        }

        // These ones are not necessary, because we just flip the args and cmpltps or cmpleps
        // check("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
        // check("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

        check("shufps", 4, in_f32(2 * x));

        // SSE 2

        for (int w : {2, 4}) {
            check("addpd", w, f64_1 + f64_2);
            check("subpd", w, f64_1 - f64_2);
            check("mulpd", w, f64_1 * f64_2);
            check("divpd", w, f64_1 / f64_2);
            check("sqrtpd", w, sqrt(f64_2));
            check("maxpd", w, max(f64_1, f64_2));
            check("minpd", w, min(f64_1, f64_2));

            check("cmpeqpd", w, select(f64_1 == f64_2, 1.0f, 2.0f));
            // check("cmpneqpd", w, select(f64_1 != f64_2, 1.0f, 2.0f));
            // check("cmplepd", w, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("cmpltpd", w, select(f64_1 < f64_2, 1.0f, 2.0f));

            // llvm is pretty inconsistent about which ops get generated
            // for casts. We don't intend to catch these for now, so skip
            // them.

            // check("cvttpd2dq", 4, i32(f64_1));
            // check("cvtdq2pd", 4, f64(i32_1));
            // check("cvttps2dq", 4, i32(f32_1));
            // check("cvtdq2ps", 4, f32(i32_1));
            // check("cvtps2pd", 4, f64(f32_1));
            // check("cvtpd2ps", 4, f32(f64_1));

            check("paddq", w, i64_1 + i64_2);
            check("psubq", w, i64_1 - i64_2);
            check(use_avx512 ? "vpmullq" : "pmuludq", w, u64_1 * u64_2);

            const char *check_suffix = "";
            if (use_avx2 && w > 3) {
                check_suffix = "*ymm";
            }
            check(std::string("packssdw") + check_suffix, 4 * w, i16_sat(i32_1));
            check(std::string("packsswb") + check_suffix, 8 * w, i8_sat(i16_1));
            check(std::string("packuswb") + check_suffix, 8 * w, u8_sat(i16_1));
            check(std::string("packssdw") + check_suffix, 8 * w, u8_sat(i32_1));
            check(std::string("packssdw") + check_suffix, 8 * w, i8_sat(i32_1));

            // Sum-of-absolute-difference ops
            {
                const int f = 8;  // reduction factor.
                RDom r(0, f);
                check("psadbw", w, sum(u64(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("psadbw", w, sum(u32(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("psadbw", w, sum(u16(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("psadbw", w, sum(i64(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("psadbw", w, sum(i32(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("psadbw", w, sum(i16(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
            }
        }

        // SSE 3 / SSSE 3

        if (use_ssse3) {
            for (int w = 2; w <= 4; w++) {
                check("pmulhrsw", 4 * w, i16((i32(i16_1) * i32(i16_2) + 16384) >> 15));
                check("pmulhrsw", 4 * w, i16_sat((i32(i16_1) * i32(i16_2) + 16384) >> 15));
                check("pabsb", 8 * w, abs(i8_1));
                check("pabsw", 4 * w, abs(i16_1));
                check("pabsd", 2 * w, abs(i32_1));
            }

            // Horizontal ops. Our support for them uses intrinsics
            // from LLVM 9+.

            // Paradoxically, haddps is a bad way to do horizontal
            // adds down to a single scalar on most x86. A better
            // sequence (according to Peter Cordes on stackoverflow)
            // is movshdup, addps, movhlps, addss. haddps is still
            // good if you're only partially reducing and your result
            // is at least one native vector, if only to save code
            // size, but LLVM really really tries to avoid it and
            // replace it with shuffles whenever it can, so we won't
            // test for it.
            //
            // See:
            // https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-float-vector-sum-on-x86

            // For reducing down to a scalar we expect to see addps
            // and movshdup. We'll sniff for the movshdup.
            check("movshdup", 1, sum(in_f32(RDom(0, 2) + 2 * x)));
            check("movshdup", 1, sum(in_f32(RDom(0, 4) + 4 * x)));
            check("movshdup", 1, sum(in_f32(RDom(0, 16) + 16 * x)));

            // The integer horizontal add operations are pretty
            // terrible on all x86 variants, and LLVM does its best to
            // avoid generating those too, so we won't test that here
            // either.

            // Min reductions should use phminposuw when
            // possible. This only exists for u16. X86 is weird.
            check("phminposuw", 1, minimum(in_u16(RDom(0, 8) + 8 * x)));

            // Max reductions can use the same instruction by first
            // flipping the bits.
            check("phminposuw", 1, maximum(in_u16(RDom(0, 8) + 8 * x)));

            // Reductions over signed ints can flip the sign bit
            // before and after (equivalent to adding 128).
            check("phminposuw", 1, minimum(in_i16(RDom(0, 8) + 8 * x)));
            check("phminposuw", 1, maximum(in_i16(RDom(0, 8) + 8 * x)));

            // Reductions over 8-bit ints can widen first
            check("phminposuw", 1, minimum(in_u8(RDom(0, 16) + 16 * x)));
            check("phminposuw", 1, maximum(in_u8(RDom(0, 16) + 16 * x)));
            check("phminposuw", 1, minimum(in_i8(RDom(0, 16) + 16 * x)));
            check("phminposuw", 1, maximum(in_i8(RDom(0, 16) + 16 * x)));

            for (int w = 2; w <= 8; w++) {
                const char *check_pmaddubsw =
                    (use_avx2 && w >= 4) ? "vpmaddubsw" : "pmaddubsw";

                RDom r2(0, 2);
                check(check_pmaddubsw, 4 * w, saturating_sum(i16(in_u8(2 * x + r2)) * in_i8(2 * x + r2 + 32)));
                check(check_pmaddubsw, 4 * w, saturating_sum(i16(in_i8(2 * x + r2)) * in_u8(2 * x + r2 + 32)));

                // uint8 -> uint16 or int16 and int8 -> int16 horizontal widening adds should use pmaddubsw.
                check(check_pmaddubsw, 4 * w, sum(u16(in_u8(2 * x + r2))));
                check(check_pmaddubsw, 4 * w, sum(i16(in_u8(2 * x + r2))));
                check(check_pmaddubsw, 4 * w, sum(i16(in_i8(2 * x + r2))));

                check(check_pmaddubsw, 4 * w, u16(in_u8(2 * x)) + in_u8(2 * x + 1));
                check(check_pmaddubsw, 4 * w, i16(in_u8(2 * x)) + in_u8(2 * x + 1));
                check(check_pmaddubsw, 4 * w, i16(in_i8(2 * x)) + in_i8(2 * x + 1));
            }
        }

        // SSE 4.1

        for (int w = 2; w <= 8; w++) {
            // We generated pmaddwd when we do a sum of widening multiplies
            const char *check_pmaddwd =
                (use_avx2 && w >= 4) ? "vpmaddwd" : "pmaddwd";
            check(check_pmaddwd, 2 * w, i32(i16_1) * 3 + i32(i16_2) * 4);
            check(check_pmaddwd, 2 * w, i32(i16_1) * 3 - i32(i16_2) * 4);

            // And also for dot-products
            RDom r4(0, 4);
            check(check_pmaddwd, 2 * w, sum(i32(in_i16(x * 4 + r4)) * in_i16(x * 4 + r4 + 32)));

            check(check_pmaddwd, 2 * w, i32(in_i16(x * 2)) + in_i16(x * 2 + 1));

            // Also generate for widening_mul
            check(check_pmaddwd, 2 * w, i32(i16_1) * i32(i16_2));
        }

        // llvm doesn't distinguish between signed and unsigned multiplies
        // check("pmuldq", 4, i64(i32_1) * i64(i32_2));

        if (use_sse41) {
            for (int w = 2; w <= 4; w++) {
                if (!use_avx512) {
                    check("pmuludq", 2 * w, u64(u32_1) * u64(u32_2));
                }
                check("pmulld", 2 * w, i32_1 * i32_2);

                if (!use_avx512) {
                    // avx512 uses a variety of predicated mov ops instead of blend
                    check("blend*ps", 2 * w, select(f32_1 > 0.7f, f32_1, f32_2));
                    check("blend*pd", w, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));
                    check("pblend*b", 8 * w, select(u8_1 > 7, u8_1, u8_2));
                    check("pblend*b", 8 * w, select(u8_1 == 7, u8_1, u8_2));
                    check("pblend*b", 8 * w, select(u8_1 <= 7, i8_1, i8_2));
                }

                check("pmaxsb", 8 * w, max(i8_1, i8_2));
                check("pminsb", 8 * w, min(i8_1, i8_2));
                check("pmaxuw", 4 * w, max(u16_1, u16_2));
                check("pminuw", 4 * w, min(u16_1, u16_2));
                check("pmaxud", 2 * w, max(u32_1, u32_2));
                check("pminud", 2 * w, min(u32_1, u32_2));
                check("pmaxsd", 2 * w, max(i32_1, i32_2));
                check("pminsd", 2 * w, min(i32_1, i32_2));

                check("roundps", 2 * w, round(f32_1));
                check("roundpd", w, round(f64_1));
                check("roundps", 2 * w, floor(f32_1));
                check("roundpd", w, floor(f64_1));
                check("roundps", 2 * w, ceil(f32_1));
                check("roundpd", w, ceil(f64_1));

                check("pcmpeqq", w, select(i64_1 == i64_2, i64(1), i64(2)));
                check("packusdw", 4 * w, u16_sat(i32_1));
            }
        }

        // SSE 4.2
        if (use_sse42) {
            check("pcmpgtq", 2, select(i64_1 > i64_2, i64(1), i64(2)));
        }

        // AVX
        if (use_avx) {
            check("vsqrtps*ymm", 8, sqrt(f32_1));
            check("vsqrtpd*ymm", 4, sqrt(f64_1));
            check(use_avx512 ? "vrsqrt*ps" : "vrsqrtps*ymm", 8, fast_inverse_sqrt(f32_1));
            check(use_avx512 ? "vrcp*ps" : "vrcpps*ymm", 8, fast_inverse(f32_1));

#if 0
            // Not implemented in the front end.
            check("vandnps", 8, bool1 & (!bool2));
            check("vandps", 8, bool1 & bool2);
            check("vorps", 8, bool1 | bool2);
            check("vxorps", 8, bool1 ^ bool2);
#endif

            check("vaddps*ymm", 8, f32_1 + f32_2);
            check("vaddpd*ymm", 4, f64_1 + f64_2);
            check("vmulps*ymm", 8, f32_1 * f32_2);
            check("vmulpd*ymm", 4, f64_1 * f64_2);
            check("vsubps*ymm", 8, f32_1 - f32_2);
            check("vsubpd*ymm", 4, f64_1 - f64_2);
            // LLVM no longer generates division instruction when fast-math is on
            // check("vdivps", 8, f32_1 / f32_2);
            // check("vdivpd", 4, f64_1 / f64_2);
            check("vminps*ymm", 8, min(f32_1, f32_2));
            check("vminpd*ymm", 4, min(f64_1, f64_2));
            check("vmaxps*ymm", 8, max(f32_1, f32_2));
            check("vmaxpd*ymm", 4, max(f64_1, f64_2));
            check("vroundps*ymm", 8, round(f32_1));
            check("vroundpd*ymm", 4, round(f64_1));

            check("vcmpeqpd*ymm", 4, select(f64_1 == f64_2, 1.0f, 2.0f));
            // check("vcmpneqpd", 4, select(f64_1 != f64_2, 1.0f, 2.0f));
            // check("vcmplepd", 4, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("vcmpltpd*ymm", 4, select(f64_1 < f64_2, 1.0f, 2.0f));
            check("vcmpeqps*ymm", 8, select(f32_1 == f32_2, 1.0f, 2.0f));
            // check("vcmpneqps", 8, select(f32_1 != f32_2, 1.0f, 2.0f));
            // check("vcmpleps", 8, select(f32_1 <= f32_2, 1.0f, 2.0f));
            check("vcmpltps*ymm", 8, select(f32_1 < f32_2, 1.0f, 2.0f));

            // avx512 can do predicated mov ops instead of blends
            check(use_avx512 ? "vmov*%k" : "vblend*ps*ymm", 8, select(f32_1 > 0.7f, f32_1, f32_2));
            check(use_avx512 ? "vmov*%k" : "vblend*pd*ymm", 4, select(f64_1 > cast<double>(0.7f), f64_1, f64_2));

            check("vcvttps2dq*ymm", 8, i32(f32_1));
            check("vcvtdq2ps*ymm", 8, f32(i32_1));
            check(use_avx512 ? "vcvttpd2dq*ymm" : "vcvttpd2dq*xmm", 8, i32(f64_1));
            check(use_avx512 ? "vcvtdq2pd*zmm" : "vcvtdq2pd*ymm", 8, f64(i32_1));
            check(use_avx512 ? "vcvtps2pd*zmm" : "vcvtps2pd*ymm", 8, f64(f32_1));
            check(use_avx512 ? "vcvtpd2ps*ymm" : "vcvtpd2ps*xmm", 8, f32(f64_1));

            // Newer llvms will just vpshufd straight from memory for reversed loads
            // check("vperm", 8, in_f32(100-x));
        }

        // AVX 2

        if (use_avx2) {
            auto check_x86_fixed_point = [&](const std::string &suffix, const int m) {
                check("vpaddb*" + suffix, 32 * m, u8_1 + u8_2);
                check("vpsubb*" + suffix, 32 * m, u8_1 - u8_2);
                check("vpaddsb*" + suffix, 32 * m, i8_sat(i16(i8_1) + i16(i8_2)));
                check("vpsubsb*" + suffix, 32 * m, i8_sat(i16(i8_1) - i16(i8_2)));
                check("vpaddusb*" + suffix, 32 * m, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
                check("vpsubusb*" + suffix, 32 * m, u8(max(i16(u8_1) - i16(u8_2), 0)));
                check("vpaddw*" + suffix, 16 * m, u16_1 + u16_2);
                check("vpsubw*" + suffix, 16 * m, u16_1 - u16_2);
                check("vpaddsw*" + suffix, 16 * m, i16_sat(i32(i16_1) + i32(i16_2)));
                check("vpsubsw*" + suffix, 16 * m, i16_sat(i32(i16_1) - i32(i16_2)));
                check("vpaddusw*" + suffix, 16 * m, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
                check("vpsubusw*" + suffix, 16 * m, u16(max(i32(u16_1) - i32(u16_2), 0)));
                check("vpaddd*" + suffix, 8 * m, i32_1 + i32_2);
                check("vpsubd*" + suffix, 8 * m, i32_1 - i32_2);
                check("vpmulhw*" + suffix, 16 * m, i16((i32(i16_1) * i32(i16_2)) / (256 * 256)));
                check("vpmulhw*" + suffix, 16 * m, i16((i32(i16_1) * i32(i16_2)) >> cast<unsigned>(16)));
                check("vpmulhw*" + suffix, 16 * m, i16((i32(i16_1) * i32(i16_2)) >> cast<int>(16)));
                check("vpmulhw*" + suffix, 16 * m, i16((i32(i16_1) * i32(i16_2)) << cast<int>(-16)));
                check("vpmullw*" + suffix, 16 * m, i16_1 * i16_2);

                check("vpmulhrsw*" + suffix, 16 * m, i16((((i32(i16_1) * i32(i16_2)) + 16384)) / 32768));
                check("vpmulhrsw*" + suffix, 16 * m, i16_sat((((i32(i16_1) * i32(i16_2)) + 16384)) / 32768));

                check("vpcmp*b*" + suffix, 32 * m, select(u8_1 == u8_2, u8(1), u8(2)));
                check("vpcmp*b*" + suffix, 32 * m, select(u8_1 > u8_2, u8(1), u8(2)));
                check("vpcmp*w*" + suffix, 16 * m, select(u16_1 == u16_2, u16(1), u16(2)));
                check("vpcmp*w*" + suffix, 16 * m, select(u16_1 > u16_2, u16(1), u16(2)));
                check("vpcmp*d*" + suffix, 8 * m, select(u32_1 == u32_2, u32(1), u32(2)));
                check("vpcmp*d*" + suffix, 8 * m, select(u32_1 > u32_2, u32(1), u32(2)));

                check("vpavgb*" + suffix, 32 * m, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
                check("vpavgw*" + suffix, 16 * m, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
                check("vpmaxsw*" + suffix, 16 * m, max(i16_1, i16_2));
                check("vpminsw*" + suffix, 16 * m, min(i16_1, i16_2));
                check("vpmaxub*" + suffix, 32 * m, max(u8_1, u8_2));
                check("vpminub*" + suffix, 32 * m, min(u8_1, u8_2));

                check("vpabsb*" + suffix, 32 * m, abs(i8_1));
                check("vpabsw*" + suffix, 16 * m, abs(i16_1));
                check("vpabsd*" + suffix, 8 * m, abs(i32_1));

                check("vpsubusb*" + suffix, 32 * m, absd(u8_1, u8_2));
                check("vpsubusw*" + suffix, 16 * m, absd(u16_1, u16_2));
                check("vpmaxsb*" + suffix, 32 * m, absd(i8_1, i8_2));
                check("vpmaxsw*" + suffix, 16 * m, absd(i16_1, i16_2));
                check("vpmaxsd*" + suffix, 8 * m, absd(i32_1, i32_2));
            };

            check_x86_fixed_point("ymm", 1);

            if (use_avx512) {
                check_x86_fixed_point("zmm", 2);
            }

            if (target.has_feature(Target::F16C)) {
                check("vcvtps2ph", 8, cast(Float(16), f32_1));
                check("vcvtph2ps", 8, cast(Float(32), f16_1));
            }

            check(use_avx512 ? "vpaddq*zmm" : "vpaddq*ymm", 8, i64_1 + i64_2);
            check(use_avx512 ? "vpsubq*zmm" : "vpsubq*ymm", 8, i64_1 - i64_2);
            check(use_avx512 ? "vpmullq" : "vpmuludq*ymm", 8, u64_1 * u64_2);

            // llvm doesn't distinguish between signed and unsigned multiplies
            // check("vpmuldq", 8, i64(i32_1) * i64(i32_2));
            if (!use_avx512) {
                // AVX512 uses widening loads instead
                check("vpmuludq*ymm", 8, u64(u32_1) * u64(u32_2));
            }
            check("vpmulld*ymm", 8, i32_1 * i32_2);

            if (use_avx512) {
                // avx512 does vector blends with a mov + predicate register
                check("vmov*%k", 32, select(u8_1 > 7, u8_1, u8_2));
            } else {
                check("vpblend*b*ymm", 32, select(u8_1 > 7, u8_1, u8_2));
            }

            if (use_avx512) {
                check("vpmaxsb*zmm", 64, max(i8_1, i8_2));
                check("vpminsb*zmm", 64, min(i8_1, i8_2));
                check("vpmaxuw*zmm", 32, max(u16_1, u16_2));
                check("vpminuw*zmm", 32, min(u16_1, u16_2));
                check("vpmaxud*zmm", 16, max(u32_1, u32_2));
                check("vpminud*zmm", 16, min(u32_1, u32_2));
                check("vpmaxsd*zmm", 16, max(i32_1, i32_2));
                check("vpminsd*zmm", 16, min(i32_1, i32_2));
            }
            check("vpmaxsb*ymm", 32, max(i8_1, i8_2));
            check("vpminsb*ymm", 32, min(i8_1, i8_2));
            check("vpmaxuw*ymm", 16, max(u16_1, u16_2));
            check("vpminuw*ymm", 16, min(u16_1, u16_2));
            check("vpmaxud*ymm", 8, max(u32_1, u32_2));
            check("vpminud*ymm", 8, min(u32_1, u32_2));
            check("vpmaxsd*ymm", 8, max(i32_1, i32_2));
            check("vpminsd*ymm", 8, min(i32_1, i32_2));

            check("vpcmpeqq*ymm", 4, select(i64_1 == i64_2, i64(1), i64(2)));
            check("vpackusdw*ymm", 16, u16(clamp(i32_1, 0, max_u16)));
            check("vpcmpgtq*ymm", 4, select(i64_1 > i64_2, i64(1), i64(2)));

            // Sum-of-absolute-difference ops
            for (int w : {4, 8}) {
                const int f = 8;  // reduction factor.
                RDom r(0, f);
                check("vpsadbw", w, sum(u64(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("vpsadbw", w, sum(u32(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("vpsadbw", w, sum(u16(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("vpsadbw", w, sum(i64(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("vpsadbw", w, sum(i32(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
                check("vpsadbw", w, sum(i16(absd(in_u8(f * x + r), in_u8(f * x + r + 32)))));
            }
        }

        if (use_avx512) {
#if 0
            // Not yet implemented
            check("vrangeps", 16, clamp(f32_1, 3.0f, 9.0f));
            check("vrangepd", 8, clamp(f64_1, f64(3), f64(9)));

            check("vreduceps", 16, f32_1 - floor(f32_1));
            check("vreduceps", 16, f32_1 - floor(f32_1*8)/8);
            check("vreduceps", 16, f32_1 - trunc(f32_1));
            check("vreduceps", 16, f32_1 - trunc(f32_1*8)/8);
            check("vreducepd", 8, f64_1 - floor(f64_1));
            check("vreducepd", 8, f64_1 - floor(f64_1*8)/8);
            check("vreducepd", 8, f64_1 - trunc(f64_1));
            check("vreducepd", 8, f64_1 - trunc(f64_1*8)/8);
#endif
        }
        if (use_avx512) {
            check("vpabsq", 8, abs(i64_1));
            check("vpmaxuq", 8, max(u64_1, u64_2));
            check("vpminuq", 8, min(u64_1, u64_2));
            check("vpmaxsq", 8, max(i64_1, i64_2));
            check("vpminsq", 8, min(i64_1, i64_2));
        }
        if (use_avx512_vnni) {
            // For our targets, avx512_vnni implies avx512_bf16
            // Disabled due to https://github.com/halide/Halide/issues/7219
            /*
            check("vcvtne2ps2bf16*zmm", 32, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*ymm", 16, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*xmm", 8, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*xmm", 4, cast(BFloat(16), f32_1));
            */

            {
                // 16 bit, 2 element dot product
                RDom r(0, 2);
                check("vdpbf16ps*zmm", 16, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                check("vpdpwssd*zmm", 16, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                if (use_avx_vnni) {
                    check("vdpbf16ps*ymm", 8, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                    check("vdpbf16ps*xmm", 4, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                    check("vpdpwssd*ymm", 8, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                    check("vpdpwssd*xmm", 4, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                }
            }
            {
                // 8 bit, 4 element dot product
                RDom r(0, 4);
                check("vpdpbusd*zmm", 16, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusd*zmm", 16, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                if (use_avx_vnni) {
                    check("vpdpbusd*ymm", 8, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                    check("vpdpbusd*ymm", 8, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                    check("vpdpbusd*xmm", 4, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                    check("vpdpbusd*xmm", 4, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                }
            }
            {
                // 16 bit, 2 element saturaing dot product
                RDom r(0, 2);
                check("vpdpwssds*zmm", 16, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                if (use_avx_vnni) {
                    check("vpdpwssds*ymm", 8, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                    check("vpdpwssds*xmm", 4, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                }
            }
            {
                // 8 bit, 4 element saturating dot product
                RDom r(0, 4);
                check("vpdpbusds*zmm", 16, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusds*zmm", 16, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                if (use_avx_vnni) {
                    check("vpdpbusds*ymm", 8, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                    check("vpdpbusds*ymm", 8, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                    check("vpdpbusds*xmm", 4, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                    check("vpdpbusds*xmm", 4, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                }
            }
        }
    }

private:
    bool use_avx2{false};
    bool use_avx512{false};
    bool use_avx512_vnni{false};
    bool use_avx_vnni{false};
    bool use_avx{false};
    bool use_sse41{false};
    bool use_sse42{false};
    bool use_ssse3{false};
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckX86>(
        argc, argv,
        {
            Target("x86-32-linux"),
            Target("x86-32-linux-sse41"),
            // Always turn on f16c when using avx. Sandy Bridge had avx without
            // f16c, but f16c is orthogonal to everything else, so there's no
            // real reason to test avx without it.
            Target("x86-64-linux-sse41-avx-f16c"),
            Target("x86-64-linux-sse41-avx-f16c-avx2"),
            // See above: don't test avx512 without extra features, the test
            // isn't yet set up to test it properly.
            // Target("x86-64-linux-sse41-avx-avx2-avx512"),
            // Target("x86-64-linux-sse41-avx-avx2-avx512-avx512_knl"),
            Target("x86-64-linux-sse41-avx-f16c-avx2-avx512-avx512_skylake"),
            Target("x86-64-linux-sse41-avx-f16c-avx2-avx512-avx512_skylake-avx512_cannonlake"),
            Target("x86-64-linux-sse41-avx-f16c-avx2-avx512-avx512_skylake-avx512_cannonlake-avx512_zen4"),
            Target("x86-64-linux-sse41-avx-f16c-avx2-avx512-avx512_skylake-avx512_cannonlake-avx512_zen4-avx512_sapphirerapids"),
        });
}

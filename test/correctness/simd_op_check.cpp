#include "simd_op_check.h"
#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

// This tests that we can correctly generate all the simd ops
using std::string;
using std::vector;

constexpr int max_i8 = 127;
constexpr int max_i16 = 32767;
constexpr int max_i32 = 0x7fffffff;
constexpr int max_u8 = 255;
constexpr int max_u16 = 65535;
const Expr max_u32 = UInt(32).max();

class SimdOpCheck : public SimdOpCheckTest {
public:
    SimdOpCheck(Target t, int w = 768, int h = 128)
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

        use_vsx = target.has_feature(Target::VSX);
        use_power_arch_2_07 = target.has_feature(Target::POWER_ARCH_2_07);
        use_wasm_simd128 = target.has_feature(Target::WasmSimd128);
        use_wasm_sat_float_to_int = target.has_feature(Target::WasmSatFloatToInt);
        use_wasm_sign_ext = target.has_feature(Target::WasmSignExt);
    }

    void add_tests() override {
        // Queue up a bunch of tasks representing each test to run.
        if (target.arch == Target::X86) {
            check_sse_all();
        } else if (target.arch == Target::ARM) {
            check_neon_all();
        } else if (target.arch == Target::POWERPC) {
            check_altivec_all();
        } else if (target.arch == Target::WebAssembly) {
            check_wasm_all();
        }
    }

    void check_sse_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
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
                //check("divps", 2*w, f32_1 / f32_2);
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
            //check("cmpneqps", 2*w, cast<int32_t>(f32_1 != f32_2));
            //check("cmpleps", 2*w, cast<int32_t>(f32_1 <= f32_2));
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
        //check("cmpnleps", 4, select(f32_1 > f32_2, 1.0f, 2.0f));
        //check("cmpnltps", 4, select(f32_1 >= f32_2, 1.0f, 2.0f));

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
            //check("cmpneqpd", w, select(f64_1 != f64_2, 1.0f, 2.0f));
            //check("cmplepd", w, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("cmpltpd", w, select(f64_1 < f64_2, 1.0f, 2.0f));

            // llvm is pretty inconsistent about which ops get generated
            // for casts. We don't intend to catch these for now, so skip
            // them.

            //check("cvttpd2dq", 4, i32(f64_1));
            //check("cvtdq2pd", 4, f64(i32_1));
            //check("cvttps2dq", 4, i32(f32_1));
            //check("cvtdq2ps", 4, f32(i32_1));
            //check("cvtps2pd", 4, f64(f32_1));
            //check("cvtpd2ps", 4, f32(f64_1));

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
        }

        // llvm doesn't distinguish between signed and unsigned multiplies
        //check("pmuldq", 4, i64(i32_1) * i64(i32_2));

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
            //check("vdivps", 8, f32_1 / f32_2);
            //check("vdivpd", 4, f64_1 / f64_2);
            check("vminps*ymm", 8, min(f32_1, f32_2));
            check("vminpd*ymm", 4, min(f64_1, f64_2));
            check("vmaxps*ymm", 8, max(f32_1, f32_2));
            check("vmaxpd*ymm", 4, max(f64_1, f64_2));
            check("vroundps*ymm", 8, round(f32_1));
            check("vroundpd*ymm", 4, round(f64_1));

            check("vcmpeqpd*ymm", 4, select(f64_1 == f64_2, 1.0f, 2.0f));
            //check("vcmpneqpd", 4, select(f64_1 != f64_2, 1.0f, 2.0f));
            //check("vcmplepd", 4, select(f64_1 <= f64_2, 1.0f, 2.0f));
            check("vcmpltpd*ymm", 4, select(f64_1 < f64_2, 1.0f, 2.0f));
            check("vcmpeqps*ymm", 8, select(f32_1 == f32_2, 1.0f, 2.0f));
            //check("vcmpneqps", 8, select(f32_1 != f32_2, 1.0f, 2.0f));
            //check("vcmpleps", 8, select(f32_1 <= f32_2, 1.0f, 2.0f));
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
            check("vpaddb*ymm", 32, u8_1 + u8_2);
            check("vpsubb*ymm", 32, u8_1 - u8_2);
            check("vpaddsb*ymm", 32, i8_sat(i16(i8_1) + i16(i8_2)));
            check("vpsubsb*ymm", 32, i8_sat(i16(i8_1) - i16(i8_2)));
            check("vpaddusb*ymm", 32, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("vpsubusb*ymm", 32, u8(max(i16(u8_1) - i16(u8_2), 0)));
            check("vpaddw*ymm", 16, u16_1 + u16_2);
            check("vpsubw*ymm", 16, u16_1 - u16_2);
            check("vpaddsw*ymm", 16, i16_sat(i32(i16_1) + i32(i16_2)));
            check("vpsubsw*ymm", 16, i16_sat(i32(i16_1) - i32(i16_2)));
            check("vpaddusw*ymm", 16, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("vpsubusw*ymm", 16, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("vpaddd*ymm", 8, i32_1 + i32_2);
            check("vpsubd*ymm", 8, i32_1 - i32_2);
            check("vpmulhw*ymm", 16, i16((i32(i16_1) * i32(i16_2)) / (256 * 256)));
            check("vpmulhw*ymm", 16, i16((i32(i16_1) * i32(i16_2)) >> cast<unsigned>(16)));
            check("vpmulhw*ymm", 16, i16((i32(i16_1) * i32(i16_2)) >> cast<int>(16)));
            check("vpmulhw*ymm", 16, i16((i32(i16_1) * i32(i16_2)) << cast<int>(-16)));
            check("vpmullw*ymm", 16, i16_1 * i16_2);

            check("vpmulhrsw*ymm", 16, i16((((i32(i16_1) * i32(i16_2)) + 16384)) / 32768));
            check("vpmulhrsw*ymm", 16, i16_sat((((i32(i16_1) * i32(i16_2)) + 16384)) / 32768));

            check("vpcmp*b*ymm", 32, select(u8_1 == u8_2, u8(1), u8(2)));
            check("vpcmp*b*ymm", 32, select(u8_1 > u8_2, u8(1), u8(2)));
            check("vpcmp*w*ymm", 16, select(u16_1 == u16_2, u16(1), u16(2)));
            check("vpcmp*w*ymm", 16, select(u16_1 > u16_2, u16(1), u16(2)));
            check("vpcmp*d*ymm", 8, select(u32_1 == u32_2, u32(1), u32(2)));
            check("vpcmp*d*ymm", 8, select(u32_1 > u32_2, u32(1), u32(2)));

            check("vpavgb*ymm", 32, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
            check("vpavgw*ymm", 16, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
            check("vpmaxsw*ymm", 16, max(i16_1, i16_2));
            check("vpminsw*ymm", 16, min(i16_1, i16_2));
            check("vpmaxub*ymm", 32, max(u8_1, u8_2));
            check("vpminub*ymm", 32, min(u8_1, u8_2));

            check(use_avx512 ? "vpaddq*zmm" : "vpaddq*ymm", 8, i64_1 + i64_2);
            check(use_avx512 ? "vpsubq*zmm" : "vpsubq*ymm", 8, i64_1 - i64_2);
            check(use_avx512 ? "vpmullq" : "vpmuludq*ymm", 8, u64_1 * u64_2);

            check("vpabsb*ymm", 32, abs(i8_1));
            check("vpabsw*ymm", 16, abs(i16_1));
            check("vpabsd*ymm", 8, abs(i32_1));

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
        if (use_avx512 && target.has_feature(Target::AVX512_SapphireRapids)) {
            check("vcvtne2ps2bf16*zmm", 32, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*ymm", 16, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*xmm", 8, cast(BFloat(16), f32_1));
            check("vcvtneps2bf16*xmm", 4, cast(BFloat(16), f32_1));

            {
                // 16 bit, 2 element dot product
                RDom r(0, 2);
                check("vdpbf16ps*zmm", 16, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                check("vdpbf16ps*ymm", 8, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                check("vdpbf16ps*xmm", 4, sum(f32(in_bf16(2 * x + r)) * in_bf16(2 * x + r + 32)));
                check("vpdpwssd*zmm", 16, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                check("vpdpwssd*ymm", 8, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                check("vpdpwssd*xmm", 4, sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
            }
            {
                // 8 bit, 4 element dot product
                RDom r(0, 4);
                check("vpdpbusd*zmm", 16, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusd*zmm", 16, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                check("vpdpbusd*ymm", 8, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusd*ymm", 8, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                check("vpdpbusd*xmm", 4, sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusd*xmm", 4, sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
            }
            {
                // 16 bit, 2 element saturaing dot product
                RDom r(0, 2);
                check("vpdpwssds*zmm", 16, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                check("vpdpwssds*ymm", 8, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
                check("vpdpwssds*xmm", 4, saturating_sum(i32(in_i16(2 * x + r)) * in_i16(2 * x + r + 32)));
            }
            {
                // 8 bit, 4 element saturating dot product
                RDom r(0, 4);
                check("vpdpbusds*zmm", 16, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusds*zmm", 16, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                check("vpdpbusds*ymm", 8, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusds*ymm", 8, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
                check("vpdpbusds*xmm", 4, saturating_sum(i32(in_u8(4 * x + r)) * in_i8(4 * x + r + 32)));
                check("vpdpbusds*xmm", 4, saturating_sum(i32(in_i8(4 * x + r)) * in_u8(4 * x + r + 32)));
            }
        }
    }

    void check_neon_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
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

            // VLD2     X       -       Load Two-Element Structures
            // These need to be vectorized at least 2 native vectors wide,
            // so we get a full vectors' worth that we know is safe to
            // access.
            check(arm32 ? "vld2.8" : "ld2", 32 * w, in_i8(x * 2) + in_i8(x * 2 + 1));
            check(arm32 ? "vld2.8" : "ld2", 32 * w, in_u8(x * 2) + in_u8(x * 2 + 1));
            check(arm32 ? "vld2.16" : "ld2", 16 * w, in_i16(x * 2) + in_i16(x * 2 + 1));
            check(arm32 ? "vld2.16" : "ld2", 16 * w, in_u16(x * 2) + in_u16(x * 2 + 1));
            check(arm32 ? "vld2.32" : "ld2", 8 * w, in_i32(x * 2) + in_i32(x * 2 + 1));
            check(arm32 ? "vld2.32" : "ld2", 8 * w, in_u32(x * 2) + in_u32(x * 2 + 1));
            check(arm32 ? "vld2.32" : "ld2", 8 * w, in_f32(x * 2) + in_f32(x * 2 + 1));

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
            // Try to trick LLVM into generating a zext instead of a sext by making
            // LLVM think the operand never has a leading 1 bit. zext breaks LLVM's
            // pattern matching of mlal.
            check(arm32 ? "vmlal.s8" : "smlal", 8 * w, i16_1 + i16(i8_2 & 0x3) * i8_3);
            check(arm32 ? "vmlal.u8" : "umlal", 8 * w, u16_1 + u16(u8_2) * u8_3);
            check(arm32 ? "vmlal.s16" : "smlal", 4 * w, i32_1 + i32(i16_2 & 0x3) * i16_3);
            check(arm32 ? "vmlal.u16" : "umlal", 4 * w, u32_1 + u32(u16_2) * u16_3);
            check(arm32 ? "vmlal.s32" : "smlal", 2 * w, i64_1 + i64(i32_2 & 0x3) * i32_3);
            check(arm32 ? "vmlal.u32" : "umlal", 2 * w, u64_1 + u64(u32_2) * u32_3);

            // VMLSL    I       -       Multiply Subtract Long
            check(arm32 ? "vmlsl.s8" : "smlsl", 8 * w, i16_1 - i16(i8_2 & 0x3) * i8_3);
            check(arm32 ? "vmlsl.u8" : "umlsl", 8 * w, u16_1 - u16(u8_2) * u8_3);
            check(arm32 ? "vmlsl.s16" : "smlsl", 4 * w, i32_1 - i32(i16_2 & 0x3) * i16_3);
            check(arm32 ? "vmlsl.u16" : "umlsl", 4 * w, u32_1 - u32(u16_2) * u16_3);
            check(arm32 ? "vmlsl.s32" : "smlsl", 2 * w, i64_1 - i64(i32_2 & 0x3) * i32_3);
            check(arm32 ? "vmlsl.u32" : "umlsl", 2 * w, u64_1 - u64(u32_2) * u32_3);

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
            check(arm32 ? "vmovn.i16" : "xtn", 8 * w, i8(i16_1));
            check(arm32 ? "vmovn.i16" : "xtn", 8 * w, u8(u16_1));
            check(arm32 ? "vmovn.i32" : "xtn", 4 * w, i16(i32_1));
            check(arm32 ? "vmovn.i32" : "xtn", 4 * w, u16(u32_1));
            check(arm32 ? "vmovn.i64" : "xtn", 2 * w, i32(i64_1));
            check(arm32 ? "vmovn.i64" : "xtn", 2 * w, u32(u64_1));

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
                        check(arm32 ? "vpaddl.s8" : "saddlp", 8, sum_(i32(in_i8(f * x + r))));
                        check(arm32 ? "vpaddl.u8" : "uaddlp", 8, sum_(i32(in_u8(f * x + r))));
                        check(arm32 ? "vpaddl.u8" : "uaddlp", 8, sum_(u32(in_u8(f * x + r))));
                        check(arm32 ? "vpaddl.s16" : "saddlp", 4, sum_(i64(in_i16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 4, sum_(i64(in_u16(f * x + r))));
                        check(arm32 ? "vpaddl.u16" : "uaddlp", 4, sum_(u64(in_u16(f * x + r))));

                        // Note that when going from u8 to i32 like this,
                        // the vpaddl is unsigned and the vpadal is a
                        // signed, because the intermediate type is u16
                        check(arm32 ? "vpadal.s16" : "sadalp", 8, sum_(i32(in_i8(f * x + r))));
                        check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(i32(in_u8(f * x + r))));
                        check(arm32 ? "vpadal.u16" : "uadalp", 8, sum_(u32(in_u8(f * x + r))));
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
            //check(arm32 ? "vqrshrn.u64" : "uqrshrn", 2 * w, u32(min((u64_1 + 8) / 16, max_u32)));

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
            //check(arm32 ? "vraddhn.i64" : "raddhn", 2 * w, u32((u128(u64_1) + u64_2 + (Expr(uint64_t(1)) << 31)) >> 32));

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
            check(arm32 ? "vrshrn.i16" : "rshrn", 8 * w, i8((i32(i16_1) + 128) >> 8));
            check(arm32 ? "vrshrn.i32" : "rshrn", 4 * w, i16((i32_1 + 256) >> 9));
            check(arm32 ? "vrshrn.i64" : "rshrn", 2 * w, i32((i64_1 + 8) >> 4));
            check(arm32 ? "vrshrn.i16" : "rshrn", 8 * w, u8((u32(u16_1) + 128) >> 8));
            check(arm32 ? "vrshrn.i32" : "rshrn", 4 * w, u16((u64(u32_1) + 1024) >> 11));
            //check(arm32 ? "vrshrn.i64" : "rshrn", 2 * w, u32((u64_1 + 64) >> 7));

            // VRSQRTE  I, F    -       Reciprocal Square Root Estimate
            check(arm32 ? "vrsqrte.f32" : "frsqrte", 4 * w, fast_inverse_sqrt(f32_1));

            // VRSQRTS  F       -       Reciprocal Square Root Step
            check(arm32 ? "vrsqrts.f32" : "frsqrts", 4 * w, fast_inverse_sqrt(f32_1));

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
            //check(arm32 ? "vrsubhn.i64" : "rsubhn", 2 * w, u32((u64_1 - u64_2 + (Expr(uint64_t(1)) << 31)) >> 32));

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
            check(arm32 ? "vshrn.i16" : "shrn", 8 * w, i8(i16_1 / 256));
            check(arm32 ? "vshrn.i32" : "shrn", 4 * w, i16(i32_1 / 65536));
            check(arm32 ? "vshrn.i64" : "shrn", 2 * w, i32(i64_1 >> 32));
            check(arm32 ? "vshrn.i16" : "shrn", 8 * w, u8(u16_1 / 256));
            check(arm32 ? "vshrn.i32" : "shrn", 4 * w, u16(u32_1 / 65536));
            check(arm32 ? "vshrn.i64" : "shrn", 2 * w, u32(u64_1 >> 32));
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
                    string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : string("st2"), width / bits, tmp2(0, 0) + tmp2(0, 63));
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
                    string op = "vst2." + std::to_string(bits);
                    check(arm32 ? op : string("st2"), width / bits, tmp2(0, 0) + tmp2(0, 127));
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
                    string op = "vst3." + std::to_string(bits);
                    check(arm32 ? op : string("st3"), width / bits, tmp2(0, 0) + tmp2(0, 127));
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
                    string op = "vst4." + std::to_string(bits);
                    check(arm32 ? op : string("st4"), width / bits, tmp2(0, 0) + tmp2(0, 127));
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

    void check_altivec_all() {
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        //Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        // Basic AltiVec SIMD instructions.
        for (int w = 1; w <= 4; w++) {
            // Vector Integer Add Instructions.
            check("vaddsbs", 16 * w, i8_sat(i16(i8_1) + i16(i8_2)));
            check("vaddshs", 8 * w, i16_sat(i32(i16_1) + i32(i16_2)));
            check("vaddsws", 4 * w, i32_sat(i64(i32_1) + i64(i32_2)));
            check("vaddubm", 16 * w, i8_1 + i8_2);
            check("vadduhm", 8 * w, i16_1 + i16_2);
            check("vadduwm", 4 * w, i32_1 + i32_2);
            check("vaddubs", 16 * w, u8(min(u16(u8_1) + u16(u8_2), max_u8)));
            check("vadduhs", 8 * w, u16(min(u32(u16_1) + u32(u16_2), max_u16)));
            check("vadduws", 4 * w, u32(min(u64(u32_1) + u64(u32_2), max_u32)));

            // Vector Integer Subtract Instructions.
            check("vsubsbs", 16 * w, i8_sat(i16(i8_1) - i16(i8_2)));
            check("vsubshs", 8 * w, i16_sat(i32(i16_1) - i32(i16_2)));
            check("vsubsws", 4 * w, i32_sat(i64(i32_1) - i64(i32_2)));
            check("vsububm", 16 * w, i8_1 - i8_2);
            check("vsubuhm", 8 * w, i16_1 - i16_2);
            check("vsubuwm", 4 * w, i32_1 - i32_2);
            check("vsububs", 16 * w, u8(max(i16(u8_1) - i16(u8_2), 0)));
            check("vsubuhs", 8 * w, u16(max(i32(u16_1) - i32(u16_2), 0)));
            check("vsubuws", 4 * w, u32(max(i64(u32_1) - i64(u32_2), 0)));

            // Vector Integer Average Instructions.
            check("vavgsb", 16 * w, i8((i16(i8_1) + i16(i8_2) + 1) / 2));
            check("vavgub", 16 * w, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
            check("vavgsh", 8 * w, i16((i32(i16_1) + i32(i16_2) + 1) / 2));
            check("vavguh", 8 * w, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
            check("vavgsw", 4 * w, i32((i64(i32_1) + i64(i32_2) + 1) / 2));
            check("vavguw", 4 * w, u32((u64(u32_1) + u64(u32_2) + 1) / 2));

            // Vector Integer Maximum and Minimum Instructions
            check("vmaxsb", 16 * w, max(i8_1, i8_2));
            check("vmaxub", 16 * w, max(u8_1, u8_2));
            check("vmaxsh", 8 * w, max(i16_1, i16_2));
            check("vmaxuh", 8 * w, max(u16_1, u16_2));
            check("vmaxsw", 4 * w, max(i32_1, i32_2));
            check("vmaxuw", 4 * w, max(u32_1, u32_2));
            check("vminsb", 16 * w, min(i8_1, i8_2));
            check("vminub", 16 * w, min(u8_1, u8_2));
            check("vminsh", 8 * w, min(i16_1, i16_2));
            check("vminuh", 8 * w, min(u16_1, u16_2));
            check("vminsw", 4 * w, min(i32_1, i32_2));
            check("vminuw", 4 * w, min(u32_1, u32_2));

            // Vector Floating-Point Arithmetic Instructions
            check(use_vsx ? "xvaddsp" : "vaddfp", 4 * w, f32_1 + f32_2);
            check(use_vsx ? "xvsubsp" : "vsubfp", 4 * w, f32_1 - f32_2);
            check(use_vsx ? "xvmaddasp" : "vmaddfp", 4 * w, f32_1 * f32_2 + f32_3);
            // check("vnmsubfp", 4, f32_1 - f32_2 * f32_3);

            // Vector Floating-Point Maximum and Minimum Instructions
            check("vmaxfp", 4 * w, max(f32_1, f32_2));
            check("vminfp", 4 * w, min(f32_1, f32_2));
        }

        // Check these if target supports VSX.
        if (use_vsx) {
            for (int w = 1; w <= 4; w++) {
                // VSX Vector Floating-Point Arithmetic Instructions
                check("xvadddp", 2 * w, f64_1 + f64_2);
                check("xvmuldp", 2 * w, f64_1 * f64_2);
                check("xvsubdp", 2 * w, f64_1 - f64_2);
                check("xvaddsp", 4 * w, f32_1 + f32_2);
                check("xvmulsp", 4 * w, f32_1 * f32_2);
                check("xvsubsp", 4 * w, f32_1 - f32_2);
                check("xvmaxdp", 2 * w, max(f64_1, f64_2));
                check("xvmindp", 2 * w, min(f64_1, f64_2));
            }
        }

        // Check these if target supports POWER ISA 2.07 and above.
        // These also include new instructions in POWER ISA 2.06.
        if (use_power_arch_2_07) {
            for (int w = 1; w <= 4; w++) {
                check("vaddudm", 2 * w, i64_1 + i64_2);
                check("vsubudm", 2 * w, i64_1 - i64_2);

                check("vmaxsd", 2 * w, max(i64_1, i64_2));
                check("vmaxud", 2 * w, max(u64_1, u64_2));
                check("vminsd", 2 * w, min(i64_1, i64_2));
                check("vminud", 2 * w, min(u64_1, u64_2));
            }
        }
    }

    void check_wasm_all() {
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_1 < -0.3f), bool_3 = (f32_1 != -0.34f);

        check("f32.sqrt", 1, sqrt(f32_1));
        check("f32.min", 1, min(f32_1, f32_2));
        check("f32.max", 1, max(f32_1, f32_2));
        check("f32.ceil", 1, ceil(f32_1));
        check("f32.floor", 1, floor(f32_1));
        check("f32.trunc", 1, trunc(f32_1));
        check("f32.nearest", 1, round(f32_1));
        check("f32.abs", 1, abs(f32_1));
        check("f32.neg", 1, -f32_1);

        if (use_wasm_sat_float_to_int) {
            check("i32.trunc_sat_f32_s", 1, i32(f32_1));
            check("i32.trunc_sat_f32_u", 1, u32(f32_1));
            check("i32.trunc_sat_f64_s", 1, i32(f64_1));
            check("i32.trunc_sat_f64_u", 1, u32(f64_1));

            check("i64.trunc_sat_f32_s", 1, i64(f32_1));
            check("i64.trunc_sat_f32_u", 1, u64(f32_1));
            check("i64.trunc_sat_f64_s", 1, i64(f64_1));
            check("i64.trunc_sat_f64_u", 1, u64(f64_1));
        }

        if (use_wasm_sign_ext) {
            // TODO(https://github.com/halide/Halide/issues/5130):
            // current LLVM doesn't reliably emit i32.extend8_s here --
            // but the same bitcode does work when run thru llc. Very odd.
            //
            // check("i32.extend8_s", 1, i32(i8(x) ^ 1));
            // check("i32.extend16_s", 1, i32(i16(x) ^ 1));
            // check("i64.extend8_s", 1, i64(i8(x) ^ 1));
            // check("i64.extend16_s", 1, i64(i16(x) ^ 1));
            // check("i64.extend32_s", 1, i64(i32(x) ^ 1));
        }

        if (use_wasm_simd128) {
            for (int w = 1; w <= 4; w <<= 1) {
                // create arbitrary 16-byte constant
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("v128.const", 16 * w, u8_1 * u8(42 + x));
                }

                // Create vector with identical lanes
                // (Note that later LLVMs will use 64-bit constants for some smaller splats)
                check("i8x16.splat", 16 * w, u8_1 * u8(42));
                if (Halide::Internal::get_llvm_version() >= 130) {
                    // LLVM13 likes to emit all of these as v128.const
                    check("v128.const", 8 * w, u16_1 * u16(42));
                    check("v128.const", 4 * w, u32_1 * u32(42));
                    check("v128.const", 2 * w, u64_1 * u64(42));
                    check("v128.const", 8 * w, f32_1 * f32(42));
                    check("v128.const", 4 * w, f64_1 * f64(42));
                } else {
                    check("i64x2.splat", 8 * w, u16_1 * u16(42));
                    check("i64x2.splat", 4 * w, u32_1 * u32(42));
                    check("i64x2.splat", 2 * w, u64_1 * u64(42));
                    check("f32x4.splat", 8 * w, f32_1 * f32(42));
                    check("f64x2.splat", 4 * w, f64_1 * f64(42));
                }

                // Extract lane as a scalar (extract_lane)
                // Replace lane value (replace_lane)
                // Skipped: there aren't really idioms where we desire these
                // to be used explicitly

                // Shuffling using immediate indices
                check("i8x16.shuffle", 16 * w, in_u8(2 * x));
                check("i8x16.shuffle", 8 * w, in_u16(2 * x));
                check("i8x16.shuffle", 4 * w, in_u32(2 * x));

                // Swizzling using variable indices
                // (This fails to generate, but that's not entirely surprising -- I don't
                // think we ever attempt to emit the most general-purpose swizzles in Halide
                // code, so this may or may not be a defect.)
                //
                // TODO: this currently emits a bunch of extract_lane / replace_lane ops,
                // so we should definitely try to do better.
                // check("v8x16.swizzle", 16*w, in_u8(in_u8(x+32)));

                // Integer addition
                check("i8x16.add", 16 * w, i8_1 + i8_2);
                check("i16x8.add", 8 * w, i16_1 + i16_2);
                check("i32x4.add", 4 * w, i32_1 + i32_2);
                check("i64x2.add", 2 * w, i64_1 + i64_2);

                // Integer subtraction
                check("i8x16.sub", 16 * w, i8_1 - i8_2);
                check("i16x8.sub", 8 * w, i16_1 - i16_2);
                check("i32x4.sub", 4 * w, i32_1 - i32_2);
                check("i64x2.sub", 2 * w, i64_1 - i64_2);

                // Integer multiplication
                // WASM-simd doesn't have an i8x16.mul operation.
                // check("i8x16.mul", 16 * w, i8_1 * i8_2);
                check("i16x8.mul", 8 * w, i16_1 * i16_2);
                check("i32x4.mul", 4 * w, i32_1 * i32_2);
                check("i64x2.mul", 2 * w, i64_1 * i64_2);

                if (Halide::Internal::get_llvm_version() >= 130) {
                    // Integer dot product (16 -> 32)
                    for (int f : {2, 4, 8}) {
                        RDom r(0, f);
                        for (int v : {1, 2, 4}) {
                            check("i32x4.dot_i16x8_s", w * v, sum(i32(in_i16(f * x + r)) * in_i16(f * x + r + 32)));
                        }
                    }
                }

                // Integer negation
                check("i8x16.neg", 16 * w, -i8_1);
                check("i16x8.neg", 8 * w, -i16_1);
                check("i32x4.neg", 4 * w, -i32_1);
                check("i64x2.neg", 2 * w, -i64_1);

                if (Halide::Internal::get_llvm_version() >= 130) {
                    // At present, we only attempt to generate these for LLVM >= 13.

                    // Extended (widening) integer multiplication
                    if (w > 1) {
                        // Need a register wider than 128 bits for us to generate these
                        check("i16x8.extmul_low_i8x16_s", 8 * w, i16(i8_1) * i8_2);
                        check("i32x4.extmul_low_i16x8_s", 4 * w, i32(i16_1) * i16_2);
                        check("i64x2.extmul_low_i32x4_s", 2 * w, i64(i32_1) * i32_2);
                        check("i16x8.extmul_low_i8x16_u", 8 * w, u16(u8_1) * u8_2);
                        check("i32x4.extmul_low_i16x8_u", 4 * w, u32(u16_1) * u16_2);
                        check("i64x2.extmul_low_i32x4_u", 2 * w, u64(u32_1) * u32_2);
                        check("i16x8.extmul_high_i8x16_s", 8 * w, i16(i8_1) * i8_2);
                        check("i32x4.extmul_high_i16x8_s", 4 * w, i32(i16_1) * i16_2);
                        check("i64x2.extmul_high_i32x4_s", 2 * w, i64(i32_1) * i32_2);
                        check("i16x8.extmul_high_i8x16_u", 8 * w, u16(u8_1) * u8_2);
                        check("i32x4.extmul_high_i16x8_u", 4 * w, u32(u16_1) * u16_2);
                        check("i64x2.extmul_high_i32x4_u", 2 * w, u64(u32_1) * u32_2);
                    }

                    // Extended pairwise integer addition
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

                        check("i16x8.extadd_pairwise_i8x16_s", 8 * w, sum_(i16(in_i8(f * x + r))));
                        check("i16x8.extadd_pairwise_i8x16_u", 8 * w, sum_(u16(in_u8(f * x + r))));
                        // The u8->i16 op uses the unsigned variant
                        check("i16x8.extadd_pairwise_i8x16_u", 8 * w, sum_(i16(in_u8(f * x + r))));

                        check("i32x4.extadd_pairwise_i16x8_s", 8 * w, sum_(i32(in_i16(f * x + r))));
                        check("i32x4.extadd_pairwise_i16x8_u", 8 * w, sum_(u32(in_u16(f * x + r))));
                        // The u16->i32 op uses the unsigned variant
                        check("i32x4.extadd_pairwise_i16x8_u", 8 * w, sum_(i32(in_u16(f * x + r))));
                    }
                }

                // Saturating integer addition
                std::string sat = Halide::Internal::get_llvm_version() >= 130 ? "sat" : "saturate";
                check("i8x16.add_" + sat + "_s", 16 * w, i8_sat(i16(i8_1) + i16(i8_2)));
                check("i8x16.add_" + sat + "_u", 16 * w, u8_sat(u16(u8_1) + u16(u8_2)));
                check("i16x8.add_" + sat + "_s", 8 * w, i16_sat(i32(i16_1) + i32(i16_2)));
                check("i16x8.add_" + sat + "_u", 8 * w, u16_sat(u32(u16_1) + u32(u16_2)));

                // Saturating integer subtraction
                check("i8x16.sub_" + sat + "_s", 16 * w, i8_sat(i16(i8_1) - i16(i8_2)));
                check("i16x8.sub_" + sat + "_s", 8 * w, i16_sat(i32(i16_1) - i32(i16_2)));
                // N.B. Saturating subtracts are expressed by widening to a *signed* type
                check("i8x16.sub_" + sat + "_u", 16 * w, u8_sat(i16(u8_1) - i16(u8_2)));
                check("i16x8.sub_" + sat + "_u", 8 * w, u16_sat(i32(u16_1) - i32(u16_2)));

                if (Halide::Internal::get_llvm_version() >= 130) {
                    // Saturating integer Q-format rounding multiplication
                    // Note: division in Halide always rounds down (not towards
                    // zero). Otherwise these patterns would be more complicated.
                    check("i16x8.q15mulr_sat_s", 8 * w, i16_sat((i32(i16_1) * i32(i16_2) + (1 << 14)) / (1 << 15)));
                }

                // Lane-wise integer minimum
                check("i8x16.min_s", 16 * w, min(i8_1, i8_2));
                check("i16x8.min_s", 8 * w, min(i16_1, i16_2));
                check("i32x4.min_s", 4 * w, min(i32_1, i32_2));
                check("i8x16.min_u", 16 * w, min(u8_1, u8_2));
                check("i16x8.min_u", 8 * w, min(u16_1, u16_2));
                check("i32x4.min_u", 4 * w, min(u32_1, u32_2));

                // Lane-wise integer maximum
                check("i8x16.max_s", 16 * w, max(i8_1, i8_2));
                check("i16x8.max_s", 8 * w, max(i16_1, i16_2));
                check("i32x4.max_s", 4 * w, max(i32_1, i32_2));
                check("i8x16.max_u", 16 * w, max(u8_1, u8_2));
                check("i16x8.max_u", 8 * w, max(u16_1, u16_2));
                check("i32x4.max_u", 4 * w, max(u32_1, u32_2));

                // Lane-wise integer rounding average
                check("i8x16.avgr_u", 8 * w, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
                check("i8x16.avgr_u", 8 * w, u8((u16(u8_1) + u16(u8_2) + 1) >> 1));
                check("i16x8.avgr_u", 4 * w, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
                check("i16x8.avgr_u", 4 * w, u16((u32(u16_1) + u32(u16_2) + 1) >> 1));

                // Lane-wise integer absolute value
                check("i8x16.abs", 16 * w, abs(i8_1));
                check("i16x8.abs", 8 * w, abs(i16_1));
                check("i32x4.abs", 4 * w, abs(i32_1));
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i64x2.abs", 2 * w, abs(i64_1));
                }

                // Left shift by constant scalar
                check("i8x16.shl", 16 * w, i8_1 << i8(7));
                check("i16x8.shl", 8 * w, i16_1 << i16(7));
                check("i32x4.shl", 4 * w, i32_1 << i32(7));
                check("i64x2.shl", 2 * w, i64_1 << i64(7));
                // unsigned
                check("i8x16.shl", 16 * w, u8_1 << u8(7));
                check("i16x8.shl", 8 * w, u16_1 << u16(7));
                check("i32x4.shl", 4 * w, u32_1 << u32(7));
                check("i64x2.shl", 2 * w, u64_1 << u64(7));

                // Left shift by variable-but-uniform-across-all-lanes scalar
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("i8x16.shl",   16*w, i8_1 << in_i8(0));
                // check("i16x8.shl",   8*w, i16_1 << in_i16(0));
                // check("i32x4.shl",   4*w, i32_1 << in_i32(0));
                // check("i64x2.shl",   2*w, i64_1 << in_i64(0));
                // check("i8x16.shl",   16*w, u8_1 << in_u8(0));
                // check("i16x8.shl",   8*w, u16_1 << in_u16(0));
                // check("i32x4.shl",   4*w, u32_1 << in_u32(0));
                // check("i64x2.shl",   2*w, u64_1 << in_u64(0));

                // Right shift by constant scalar
                check("i8x16.shr_s", 16 * w, i8_1 >> i8(7));
                check("i16x8.shr_s", 8 * w, i16_1 >> i16(7));
                check("i32x4.shr_s", 4 * w, i32_1 >> i32(7));
                check("i64x2.shr_s", 2 * w, i64_1 >> i64(7));
                // unsigned
                check("i8x16.shr_u", 16 * w, u8_1 >> i8(7));
                check("i16x8.shr_u", 8 * w, u16_1 >> i16(7));
                check("i32x4.shr_u", 4 * w, u32_1 >> i32(7));
                check("i64x2.shr_u", 2 * w, u64_1 >> i64(7));

                // Right shift by variable-but-uniform-across-all-lanes scalar
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("i8x16.shr_s",   16*w, i8_1 >> in_i8(0));
                // check("i16x8.shr_s",   8*w, i16_1 >> in_i16(0));
                // check("i32x4.shr_s",   4*w, i32_1 >> in_i32(0));
                // check("i64x2.shr_s",   2*w, i64_1 >> in_i64(0));
                // check("i8x16.shr_u",   16*w, u8_1 >> in_i8(0));
                // check("i16x8.shr_u",   8*w, u16_1 >> in_i16(0));
                // check("i32x4.shr_u",   4*w, u32_1 >> in_i32(0));
                // check("i64x2.shr_u",   2*w, u64_1 >> in_i64(0));

                // Bitwise logic
                check("v128.and", 16 * w, i8_1 & i8_2);
                check("v128.and", 8 * w, i16_1 & i16_2);
                check("v128.and", 4 * w, i32_1 & i32_2);
                check("v128.and", 2 * w, i64_1 & i64_2);

                check("v128.or", 16 * w, i8_1 | i8_2);
                check("v128.or", 8 * w, i16_1 | i16_2);
                check("v128.or", 4 * w, i32_1 | i32_2);
                check("v128.or", 2 * w, i64_1 | i64_2);

                check("v128.xor", 16 * w, i8_1 ^ i8_2);
                check("v128.xor", 8 * w, i16_1 ^ i16_2);
                check("v128.xor", 4 * w, i32_1 ^ i32_2);
                check("v128.xor", 2 * w, i64_1 ^ i64_2);

                check("v128.not", 16 * w, ~i8_1);
                check("v128.not", 8 * w, ~i16_1);
                check("v128.not", 4 * w, ~i32_1);
                check("v128.not", 2 * w, ~i64_1);

                check("v128.andnot", 16 * w, i8_1 & ~i8_2);
                check("v128.andnot", 8 * w, i16_1 & ~i16_2);
                check("v128.andnot", 4 * w, i32_1 & ~i32_2);
                check("v128.andnot", 2 * w, i64_1 & ~i64_2);

                // Bitwise select
                check("v128.bitselect", 16 * w, ((u8_1 & u8_3) | (u8_2 & ~u8_3)));
                check("v128.bitselect", 8 * w, ((u16_1 & u16_3) | (u16_2 & ~u16_3)));
                check("v128.bitselect", 4 * w, ((u32_1 & u32_3) | (u32_2 & ~u32_3)));
                check("v128.bitselect", 2 * w, ((u64_1 & u64_3) | (u64_2 & ~u64_3)));

                check("v128.bitselect", 16 * w, select(bool_1, u8_1, u8_2));
                check("v128.bitselect", 8 * w, select(bool_1, u16_1, u16_2));
                check("v128.bitselect", 4 * w, select(bool_1, u32_1, u32_2));
                check("v128.bitselect", 2 * w, select(bool_1, u64_1, u64_2));
                check("v128.bitselect", 4 * w, select(bool_1, f32_1, f32_2));
                check("v128.bitselect", 2 * w, select(bool_1, f64_1, f64_2));

                // Lane-wise Population Count
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("i8x16.popcnt", 8 * w, popcount(i8_1));
                // check("i8x16.popcnt", 8 * w, popcount(u8_1));
                // check("i8x16.popcnt", 8 * w, popcount(i16_1));
                // check("i8x16.popcnt", 8 * w, popcount(u16_1));
                // check("i8x16.popcnt", 8 * w, popcount(i32_1));
                // check("i8x16.popcnt", 8 * w, popcount(u32_1));

                // Any lane true -- for VectorReduce::Or on 8-bit data
                // All lanes true  -- for VectorReduce::And on 8-bit data
                // TODO: does Halide have any idiom that could usefully use these?
                // - v128.any_true could be used for VectorReduce::Or with type bool.
                // - i8x16.all_true could be used for VectorReduce::And with type bool.
                // - the other all_true variants seem unlikely to be obviously useful in Halide.

                // Bitmask extraction
                // TODO: does Halide have any idiom that could usefully use these?
                // They all extract the high bit of each lane and return a scalar mask of them.
                // These all seem unlikely to be obviously useful in Halide.
                // check("i8x16.bitmask", 16 * w, ???);
                // check("i16x8.bitmask", 8 * w, ???);
                // check("i32x4.bitmask", 4 * w, ???);
                // check("i64x2.bitmask", 2 * w, ???);

                // Equality
                check("i8x16.eq", 16 * w, i8_1 == i8_2);
                check("i16x8.eq", 8 * w, i16_1 == i16_2);
                check("i32x4.eq", 4 * w, i32_1 == i32_2);
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i64x2.eq", 2 * w, i64_1 == i64_2);
                }
                check("f32x4.eq", 4 * w, f32_1 == f32_2);
                check("f64x2.eq", 2 * w, f64_1 == f64_2);

                // Non-equality
                check("i8x16.ne", 16 * w, i8_1 != i8_2);
                check("i16x8.ne", 8 * w, i16_1 != i16_2);
                check("i32x4.ne", 4 * w, i32_1 != i32_2);
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i64x2.ne", 2 * w, i64_1 != i64_2);
                }
                check("f32x4.ne", 4 * w, f32_1 != f32_2);
                check("f64x2.ne", 2 * w, f64_1 != f64_2);

                // Less than
                check("i8x16.lt_s", 16 * w, i8_1 < i8_2);
                check("i8x16.lt_u", 16 * w, u8_1 < u8_2);
                check("i16x8.lt_s", 8 * w, i16_1 < i16_2);
                check("i16x8.lt_u", 8 * w, u16_1 < u16_2);
                check("i32x4.lt_s", 4 * w, i32_1 < i32_2);
                check("i32x4.lt_u", 4 * w, u32_1 < u32_2);
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i64x2.lt_s", 2 * w, i64_1 < i64_2);
                }
                check("f32x4.lt", 4 * w, f32_1 < f32_2);
                check("f64x2.lt", 2 * w, f64_1 < f64_2);

                // Less than or equal
                check("i8x16.le_s", 16 * w, i8_1 <= i8_2);
                check("i8x16.le_u", 16 * w, u8_1 <= u8_2);
                check("i16x8.le_s", 8 * w, i16_1 <= i16_2);
                check("i16x8.le_u", 8 * w, u16_1 <= u16_2);
                check("i32x4.le_s", 4 * w, i32_1 <= i32_2);
                check("i32x4.le_u", 4 * w, u32_1 <= u32_2);
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i64x2.le_s", 2 * w, i64_1 <= i64_2);
                }
                check("f32x4.le", 4 * w, f32_1 <= f32_2);
                check("f64x2.le", 2 * w, f64_1 <= f64_2);

                // Greater than
                // SKIPPED: Halide aggressively simplifies > into <= so we shouldn't see these

                // Greater than or equal
                // SKIPPED: Halide aggressively simplifies >= into < so we shouldn't see these

                // Load
                check("v128.load", 16 * w, i8_1);
                check("v128.load", 8 * w, i16_1);
                check("v128.load", 4 * w, i32_1);
                check("v128.load", 4 * w, f32_1);
                check("v128.load", 2 * w, f64_1);

                // Load and Zero-Pad
                // TODO
                // check("v128.load32_zero", 2 * w, in_u32(0));
                // check("v128.load64_zero", 2 * w, in_u64(0));

                // Load vector with identical lanes
                check("v128.load8_splat", 16 * w, in_u8(0));
                check("v128.load16_splat", 8 * w, in_u16(0));
                check("v128.load32_splat", 4 * w, in_u32(0));
                check("v128.load64_splat", 2 * w, in_u64(0));

                // Load Lane
                // TODO: does Halide have any idiom that obviously generates these?

                // Load and Extend
                if (w == 1) {
                    check("i16x8.load8x8_s", 8 * w, i16(i8_1));
                    check("i16x8.load8x8_u", 8 * w, u16(u8_1));
                    check("i32x4.load16x4_s", 4 * w, i32(i16_1));
                    check("i32x4.load16x4_u", 4 * w, u32(u16_1));
                    check("i64x2.load32x2_s", 2 * w, i64(i32_1));
                    check("i64x2.load32x2_u", 2 * w, u64(u32_1));
                }

                // Store
                check("v128.store", 16 * w, i8_1);
                check("v128.store", 8 * w, i16_1);
                check("v128.store", 4 * w, i32_1);
                check("v128.store", 4 * w, f32_1);
                check("v128.store", 2 * w, f64_1);

                // Store Lane
                // TODO: does Halide have any idiom that obviously generates these?

                // Negation
                check("f32x4.neg", 4 * w, -f32_1);
                check("f64x2.neg", 2 * w, -f64_1);

                // Absolute value
                check("f32x4.abs", 4 * w, abs(f32_1));
                check("f64x2.abs", 2 * w, abs(f64_1));

                // NaN-propagating minimum
                check("f32x4.min", 4 * w, min(f32_1, f32_2));
                check("f64x2.min", 2 * w, min(f64_1, f64_2));

                // NaN-propagating maximum
                check("f32x4.max", 4 * w, max(f32_1, f32_2));
                check("f64x2.max", 2 * w, max(f64_1, f64_2));

                // Pseudo-minimum
                // Pseudo-maximum
                // TODO: does Halide have any idiom that obviously generates these?

                // Floating-point addition
                check("f32x4.add", 4 * w, f32_1 + f32_2);
                check("f64x2.add", 2 * w, f64_1 + f64_2);

                // Floating-point subtraction
                check("f32x4.sub", 4 * w, f32_1 - f32_2);
                check("f64x2.sub", 2 * w, f64_1 - f64_2);

                // Floating-point division
                check("f32x4.div", 4 * w, f32_1 / f32_2);
                check("f64x2.div", 2 * w, f64_1 / f64_2);

                // Floating-point multiplication
                check("f32x4.mul", 4 * w, f32_1 * f32_2);
                check("f64x2.mul", 2 * w, f64_1 * f64_2);

                // Square root
                check("f32x4.sqrt", 4 * w, sqrt(f32_1));
                check("f64x2.sqrt", 2 * w, sqrt(f64_1));

                if (Halide::Internal::get_llvm_version() >= 130) {
                    // Round to integer above (ceiling)
                    check("f32x4.ceil", 4 * w, ceil(f32_1));
                    check("f64x2.ceil", 2 * w, ceil(f64_1));

                    // Round to integer below (floor)
                    check("f32x4.floor", 4 * w, floor(f32_1));
                    check("f64x2.floor", 2 * w, floor(f64_1));

                    // Round to integer toward zero (truncate to integer)
                    check("f32x4.trunc", 4 * w, trunc(f32_1));
                    check("f64x2.trunc", 2 * w, trunc(f64_1));

                    // Round to nearest integer, ties to even)
                    check("f32x4.nearest", 4 * w, round(f32_1));
                    check("f64x2.nearest", 2 * w, round(f64_1));
                }

                // Integer to single-precision floating point
                check("f32x4.convert_i32x4_s", 8 * w, cast<float>(i32_1));
                check("f32x4.convert_i32x4_u", 8 * w, cast<float>(u32_1));

                // Integer to double-precision floating point
                if (Halide::Internal::get_llvm_version() >= 140) {
                    check("f64x2.convert_low_i32x4_s", 2 * w, cast<double>(i32_1));
                    check("f64x2.convert_low_i32x4_u", 2 * w, cast<double>(u32_1));
                }

                // Single-precision floating point to integer with saturation
                check("i32x4.trunc_sat_f32x4_s", 4 * w, cast<int32_t>(f32_1));
                check("i32x4.trunc_sat_f32x4_u", 4 * w, cast<uint32_t>(f32_1));

                // Double-precision floating point to integer with saturation
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("i32x4.trunc_sat_f64x2_s_zero", 4 * w, cast<int32_t>(f64_1));
                // check("i32x4.trunc_sat_f64x2_u_zero", 4 * w, cast<uint32_t>(f64_1));

                // Double-precision floating point to single-precision
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("f32x4.demote_f64x2_zero", 4 * w, ???);

                // Single-precision floating point to double-precision
                if (Halide::Internal::get_llvm_version() >= 140) {
                    // TODO(https://github.com/halide/Halide/issues/5130): broken for > 128bit vector widths
                    if (w < 2) {
                        check("f64x2.promote_low_f32x4", 2 * w, cast<double>(f32_1));
                    }
                } else if (Halide::Internal::get_llvm_version() >= 130) {
                    check("f64x2.promote_low_f32x4", 2 * w, cast<double>(f32_1));
                }

                // Integer to integer narrowing
                if (Halide::Internal::get_llvm_version() >= 130) {
                    check("i8x16.narrow_i16x8_s", 16 * w, i8_sat(i16_1));
                    check("i8x16.narrow_i16x8_u", 16 * w, u8_sat(i16_1));
                    check("i16x8.narrow_i32x4_s", 8 * w, i16_sat(i32_1));
                    check("i16x8.narrow_i32x4_u", 8 * w, u16_sat(i32_1));
                }

                // Integer to integer widening
                // TODO(https://github.com/halide/Halide/issues/5130): NOT BEING GENERATED AT TRUNK
                // check("i16x8.extend_low_i8x16_s", 8*w, i8(x) * 2);
                // check("i16x8.extend_high_i8x16_s", 8*w, i16(i8_1));
                // check("i16x8.extend_low_i8x16_u", 8*w, u8(x) * 2);
                // check("i16x8.extend_high_i8x16_u", 8*w, u16(u8_1));
                // check("i32x4.extend_low_i16x8_s", 4*w, i32(i16_1));
                // check("i32x4.extend_high_i16x8_s", 4*w, i32(i16_1));
                // check("i32x4.extend_low_i16x8_u", 4*w, u32(u16_1));
                // check("i32x4.extend_high_i16x8_u", 4*w, u32(u16_1));
                // check("i64x2.extend_low_i32x4_s", 2*w, i64(i32_1));
                // check("i64x2.extend_high_i32x4_s", 2*w, i64(i32_1));
                // check("i64x2.extend_low_i32x4_u", 2*w, u64(u32_1));
                // check("i64x2.extend_high_i32x4_u", 2*w, u64(u32_1));
            }
        }
    }

private:
    bool use_avx2{false};
    bool use_avx512{false};
    bool use_avx{false};
    bool use_power_arch_2_07{false};
    bool use_sse41{false};
    bool use_sse42{false};
    bool use_ssse3{false};
    bool use_vsx{false};
    bool use_wasm_simd128{false};
    bool use_wasm_sat_float_to_int{false};
    bool use_wasm_sign_ext{false};
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    Target host = get_host_target();
    Target hl_target = get_target_from_environment();
    printf("host is:      %s\n", host.to_string().c_str());
    printf("HL_TARGET is: %s\n", hl_target.to_string().c_str());

    SimdOpCheck test(hl_target);

    if (argc > 1) {
        test.filter = argv[1];
        test.set_num_threads(1);
    }

    if (getenv("HL_SIMD_OP_CHECK_FILTER")) {
        test.filter = getenv("HL_SIMD_OP_CHECK_FILTER");
    }

    const int seed = argc > 2 ? atoi(argv[2]) : time(nullptr);
    std::cout << "simd_op_check test seed: " << seed << "\n";
    test.set_seed(seed);

    // TODO: multithreading here is the cause of https://github.com/halide/Halide/issues/3669;
    // the fundamental issue is that we make one set of ImageParams to construct many
    // Exprs, then realize those Exprs on arbitrary threads; it is known that sharing
    // one Func across multiple threads is not guaranteed to be safe, and indeed, TSAN
    // reports data races, of which some are likely 'benign' (e.g. Function.freeze) but others
    // are highly suspect (e.g. Function.lock_loop_levels). Since multithreading here
    // was added just to avoid having this test be the last to finish, the expedient 'fix'
    // for now is to remove the multithreading. A proper fix could be made by restructuring this
    // test so that every Expr constructed for testing was guaranteed to share no Funcs
    // (Function.deep_copy() perhaps). Of course, it would also be desirable to allow Funcs, Exprs, etc
    // to be usable across multiple threads, but that is a major undertaking that is
    // definitely not worthwhile for present Halide usage patterns.
    test.set_num_threads(1);

    if (argc > 2) {
        // Don't forget: if you want to run the standard tests to a specific output
        // directory, you'll need to invoke with the first arg enclosed
        // in quotes (to avoid it being wildcard-expanded by the shell):
        //
        //    correctness_simd_op_check "*" /path/to/output
        //
        test.output_directory = argv[2];
    }

    bool success = test.test_all();

    // Compile a runtime for this target, for use in the static test.
    compile_standalone_runtime(test.output_directory + "simd_op_check_runtime.o", test.target);

    if (!success) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}

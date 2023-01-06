#include "simd_op_check.h"

#include "Halide.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

class SimdOpCheckWASM : public SimdOpCheckTest {
public:
    SimdOpCheckWASM(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
        use_wasm_simd128 = target.has_feature(Target::WasmSimd128);
        use_wasm_sat_float_to_int = target.has_feature(Target::WasmSatFloatToInt);
        use_wasm_sign_ext = target.has_feature(Target::WasmSignExt);
    }

    void add_tests() override {
        if (target.arch == Target::WebAssembly) {
            check_wasm_all();
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
                check("v128.const", 16 * w, u8_1 * u8(42 + x));

                // Create vector with identical lanes
                // (Note that later LLVMs will use 64-bit constants for some smaller splats)
                check("i8x16.splat", 16 * w, u8_1 * u8(42));
                // LLVM13 likes to emit all of these as v128.const
                check("v128.const", 8 * w, u16_1 * u16(42));
                check("v128.const", 4 * w, u32_1 * u32(42));
                check("v128.const", 2 * w, u64_1 * u64(42));
                check("v128.const", 8 * w, f32_1 * f32(42));
                check("v128.const", 4 * w, f64_1 * f64(42));

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

                // Integer dot product (16 -> 32)
                for (int f : {2, 4, 8}) {
                    RDom r(0, f);
                    for (int v : {1, 2, 4}) {
                        check("i32x4.dot_i16x8_s", w * v, sum(i32(in_i16(f * x + r)) * in_i16(f * x + r + 32)));
                    }
                }

                // Integer negation
                check("i8x16.neg", 16 * w, -i8_1);
                check("i16x8.neg", 8 * w, -i16_1);
                check("i32x4.neg", 4 * w, -i32_1);
                check("i64x2.neg", 2 * w, -i64_1);

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

                // Saturating integer addition
                check("i8x16.add_sat_s", 16 * w, i8_sat(i16(i8_1) + i16(i8_2)));
                check("i8x16.add_sat_u", 16 * w, u8_sat(u16(u8_1) + u16(u8_2)));
                check("i16x8.add_sat_s", 8 * w, i16_sat(i32(i16_1) + i32(i16_2)));
                check("i16x8.add_sat_u", 8 * w, u16_sat(u32(u16_1) + u32(u16_2)));

                // Saturating integer subtraction
                check("i8x16.sub_sat_s", 16 * w, i8_sat(i16(i8_1) - i16(i8_2)));
                check("i16x8.sub_sat_s", 8 * w, i16_sat(i32(i16_1) - i32(i16_2)));
                // N.B. Saturating subtracts are expressed by widening to a *signed* type
                check("i8x16.sub_sat_u", 16 * w, u8_sat(i16(u8_1) - i16(u8_2)));
                check("i16x8.sub_sat_u", 8 * w, u16_sat(i32(u16_1) - i32(u16_2)));

                // Saturating integer Q-format rounding multiplication
                // Note: division in Halide always rounds down (not towards
                // zero). Otherwise these patterns would be more complicated.
                check("i16x8.q15mulr_sat_s", 8 * w, i16_sat((i32(i16_1) * i32(i16_2) + (1 << 14)) / (1 << 15)));

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
                check("i64x2.abs", 2 * w, abs(i64_1));

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
                check("i8x16.popcnt", 8 * w, popcount(i8_1));
                check("i8x16.popcnt", 8 * w, popcount(u8_1));

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
                check("i64x2.eq", 2 * w, i64_1 == i64_2);
                check("f32x4.eq", 4 * w, f32_1 == f32_2);
                check("f64x2.eq", 2 * w, f64_1 == f64_2);

                // Non-equality
                check("i8x16.ne", 16 * w, i8_1 != i8_2);
                check("i16x8.ne", 8 * w, i16_1 != i16_2);
                check("i32x4.ne", 4 * w, i32_1 != i32_2);
                check("i64x2.ne", 2 * w, i64_1 != i64_2);
                check("f32x4.ne", 4 * w, f32_1 != f32_2);
                check("f64x2.ne", 2 * w, f64_1 != f64_2);

                // Less than
                check("i8x16.lt_s", 16 * w, i8_1 < i8_2);
                check("i8x16.lt_u", 16 * w, u8_1 < u8_2);
                check("i16x8.lt_s", 8 * w, i16_1 < i16_2);
                check("i16x8.lt_u", 8 * w, u16_1 < u16_2);
                check("i32x4.lt_s", 4 * w, i32_1 < i32_2);
                check("i32x4.lt_u", 4 * w, u32_1 < u32_2);
                check("i64x2.lt_s", 2 * w, i64_1 < i64_2);
                check("f32x4.lt", 4 * w, f32_1 < f32_2);
                check("f64x2.lt", 2 * w, f64_1 < f64_2);

                // Less than or equal
                check("i8x16.le_s", 16 * w, i8_1 <= i8_2);
                check("i8x16.le_u", 16 * w, u8_1 <= u8_2);
                check("i16x8.le_s", 8 * w, i16_1 <= i16_2);
                check("i16x8.le_u", 8 * w, u16_1 <= u16_2);
                check("i32x4.le_s", 4 * w, i32_1 <= i32_2);
                check("i32x4.le_u", 4 * w, u32_1 <= u32_2);
                check("i64x2.le_s", 2 * w, i64_1 <= i64_2);
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

                // Load vector with identical lanes generates *.splat.
                if (Halide::Internal::get_llvm_version() >= 160) {
                    check("i8x16.splat", 16 * w, in_u8(0));
                    check("i16x8.splat", 8 * w, in_u16(0));
                    check("i32x4.splat", 4 * w, in_u32(0));
                    check("i64x2.splat", 2 * w, in_u64(0));
                } else {
                    check("v128.load8_splat", 16 * w, in_u8(0));
                    check("v128.load16_splat", 8 * w, in_u16(0));
                    check("v128.load32_splat", 4 * w, in_u32(0));
                    check("v128.load64_splat", 2 * w, in_u64(0));
                }

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

                // Integer to single-precision floating point
                check("f32x4.convert_i32x4_s", 8 * w, cast<float>(i32_1));
                check("f32x4.convert_i32x4_u", 8 * w, cast<float>(u32_1));

                // Integer to double-precision floating point
                check("f64x2.convert_low_i32x4_s", 2 * w, cast<double>(i32_1));
                check("f64x2.convert_low_i32x4_u", 2 * w, cast<double>(u32_1));

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
                // TODO(https://github.com/halide/Halide/issues/5130): broken for > 128bit vector widths
                if (w < 2) {
                    check("f64x2.promote_low_f32x4", 2 * w, cast<double>(f32_1));
                }

                // Integer to integer narrowing
                check("i8x16.narrow_i16x8_s", 16 * w, i8_sat(i16_1));
                check("i8x16.narrow_i16x8_u", 16 * w, u8_sat(i16_1));
                check("i16x8.narrow_i32x4_s", 8 * w, i16_sat(i32_1));
                check("i16x8.narrow_i32x4_u", 8 * w, u16_sat(i32_1));

                // Integer to integer widening
                check("i16x8.extend_low_i8x16_s", 16 * w, i16(i8_1));
                check("i16x8.extend_high_i8x16_s", 16 * w, i16(i8_1));
                check("i16x8.extend_low_i8x16_u", 16 * w, u16(u8_1));
                check("i16x8.extend_high_i8x16_u", 16 * w, u16(u8_1));
                check("i32x4.extend_low_i16x8_s", 8 * w, i32(i16_1));
                check("i32x4.extend_high_i16x8_s", 8 * w, i32(i16_1));
                check("i32x4.extend_low_i16x8_u", 8 * w, u32(u16_1));
                check("i32x4.extend_high_i16x8_u", 8 * w, u32(u16_1));
                check("i64x2.extend_low_i32x4_s", 4 * w, i64(i32_1));
                check("i64x2.extend_high_i32x4_s", 4 * w, i64(i32_1));
                check("i64x2.extend_low_i32x4_u", 4 * w, u64(u32_1));
                check("i64x2.extend_high_i32x4_u", 4 * w, u64(u32_1));
            }
        }
    }

private:
    bool use_wasm_simd128{false};
    bool use_wasm_sat_float_to_int{false};
    bool use_wasm_sign_ext{false};
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckWASM>(
        argc, argv,
        {
            Target("wasm-32-wasmrt"),
            Target("wasm-32-wasmrt-wasm_simd128-wasm_sat_float_to_int"),
        });
}

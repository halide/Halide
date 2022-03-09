declare float @llvm.sqrt.f32(float);
declare <4 x float> @llvm.sqrt.v4f32(<4 x float>);
declare <2 x float> @llvm.sqrt.v2f32(<2 x float>);


; fast_inverse

define weak_odr float @fast_inverse_f32(float %x) nounwind alwaysinline {
       %y = fdiv float 1.0, %x
       ret float %y
}

define weak_odr <2 x float> @fast_inverse_f32x2(<2 x float> %x) nounwind alwaysinline {
       %y = fdiv <2 x float> <float 1.0, float 1.0>, %x
       ret <2 x float> %y
}

define weak_odr <4 x float> @fast_inverse_f32x4(<4 x float> %x) nounwind alwaysinline {
       %y = fdiv <4 x float> <float 1.0, float 1.0, float 1.0, float 1.0>, %x
       ret <4 x float> %y
}

; fast_inverse_sqrt

define weak_odr float @fast_inverse_sqrt_f32(float %x) nounwind alwaysinline {
       %y = call float @llvm.sqrt.f32(float %x)
       %z = fdiv float 1.0, %y
       ret float %z
}

define weak_odr <2 x float> @fast_inverse_sqrt_f32x2(<2 x float> %x) nounwind alwaysinline {
       %y = call <2 x float> @llvm.sqrt.v2f32(<2 x float> %x)
       %z = fdiv <2 x float> <float 1.0, float 1.0>, %y
       ret <2 x float> %z
}

define weak_odr <4 x float> @fast_inverse_sqrt_f32x4(<4 x float> %x) nounwind alwaysinline {
       %y = call <4 x float> @llvm.sqrt.v4f32(<4 x float> %x)
       %z = fdiv <4 x float> <float 1.0, float 1.0, float 1.0, float 1.0>, %y
       ret <4 x float> %z
}

; widening_mul

; i8 -> i16

define weak_odr <8 x i16> @extmul_low_s_v8i16(<16 x i8> %v1, <16 x i8> %v2) nounwind alwaysinline {
  %low1 = shufflevector <16 x i8> %v1, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %low2 = shufflevector <16 x i8> %v2, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %extended1 = sext <8 x i8> %low1 to <8 x i16>
  %extended2 = sext <8 x i8> %low2 to <8 x i16>
  %a = mul <8 x i16> %extended1, %extended2
  ret <8 x i16> %a
}

define weak_odr <8 x i16> @extmul_high_s_v8i16(<16 x i8> %v1, <16 x i8> %v2) nounwind alwaysinline {
  %high1 = shufflevector <16 x i8> %v1, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %high2 = shufflevector <16 x i8> %v2, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %extended1 = sext <8 x i8> %high1 to <8 x i16>
  %extended2 = sext <8 x i8> %high2 to <8 x i16>
  %a = mul <8 x i16> %extended1, %extended2
  ret <8 x i16> %a
}

define weak_odr <16 x i16> @widening_mul_i8x16(<16 x i8> %x, <16 x i8> %y) nounwind alwaysinline {
  %1 = tail call <8 x i16> @extmul_low_s_v8i16(<16 x i8> %x, <16 x i8> %y)
  %2 = tail call <8 x i16> @extmul_high_s_v8i16(<16 x i8> %x, <16 x i8> %y)
  %3 = shufflevector <8 x i16> %1, <8 x i16> %2, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x i16> %3
}

; i16 -> i32
define weak_odr <4 x i32> @extmul_low_s_v4i32(<8 x i16> %v1, <8 x i16> %v2) nounwind alwaysinline {
  %low1 = shufflevector <8 x i16> %v1, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %low2 = shufflevector <8 x i16> %v2, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %extended1 = sext <4 x i16> %low1 to <4 x i32>
  %extended2 = sext <4 x i16> %low2 to <4 x i32>
  %a = mul <4 x i32> %extended1, %extended2
  ret <4 x i32> %a
}

define weak_odr <4 x i32> @extmul_high_s_v4i32(<8 x i16> %v1, <8 x i16> %v2) nounwind alwaysinline {
  %high1 = shufflevector <8 x i16> %v1, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %high2 = shufflevector <8 x i16> %v2, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %extended1 = sext <4 x i16> %high1 to <4 x i32>
  %extended2 = sext <4 x i16> %high2 to <4 x i32>
  %a = mul <4 x i32> %extended1, %extended2
  ret <4 x i32> %a
}

define weak_odr <8 x i32> @widening_mul_i16x8(<8 x i16> %x, <8 x i16> %y) nounwind alwaysinline {
  %1 = tail call <4 x i32> @extmul_low_s_v4i32(<8 x i16> %x, <8 x i16> %y)
  %2 = tail call <4 x i32> @extmul_high_s_v4i32(<8 x i16> %x, <8 x i16> %y)
  %3 = shufflevector <4 x i32> %1, <4 x i32> %2, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i32> %3
}

; i32 -> i64
define weak_odr <2 x i64> @extmul_low_s_v2i64(<4 x i32> %v1, <4 x i32> %v2) nounwind alwaysinline {
  %low1 = shufflevector <4 x i32> %v1, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %low2 = shufflevector <4 x i32> %v2, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %extended1 = sext <2 x i32> %low1 to <2 x i64>
  %extended2 = sext <2 x i32> %low2 to <2 x i64>
  %a = mul <2 x i64> %extended1, %extended2
  ret <2 x i64> %a
}

define weak_odr <2 x i64> @extmul_high_s_v2i64(<4 x i32> %v1, <4 x i32> %v2) nounwind alwaysinline {
  %high1 = shufflevector <4 x i32> %v1, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %high2 = shufflevector <4 x i32> %v2, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %extended1 = sext <2 x i32> %high1 to <2 x i64>
  %extended2 = sext <2 x i32> %high2 to <2 x i64>
  %a = mul <2 x i64> %extended1, %extended2
  ret <2 x i64> %a
}

define weak_odr <4 x i64> @widening_mul_i32x4(<4 x i32> %x, <4 x i32> %y) nounwind alwaysinline {
  %1 = tail call <2 x i64> @extmul_low_s_v2i64(<4 x i32> %x, <4 x i32> %y)
  %2 = tail call <2 x i64> @extmul_high_s_v2i64(<4 x i32> %x, <4 x i32> %y)
  %3 = shufflevector <2 x i64> %1, <2 x i64> %2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i64> %3
}

; u8 -> u16
define weak_odr <8 x i16> @extmul_low_u_v8i16(<16 x i8> %v1, <16 x i8> %v2) nounwind alwaysinline {
  %low1 = shufflevector <16 x i8> %v1, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %low2 = shufflevector <16 x i8> %v2, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %extended1 = zext <8 x i8> %low1 to <8 x i16>
  %extended2 = zext <8 x i8> %low2 to <8 x i16>
  %a = mul <8 x i16> %extended1, %extended2
  ret <8 x i16> %a
}

define weak_odr <8 x i16> @extmul_high_u_v8i16(<16 x i8> %v1, <16 x i8> %v2) nounwind alwaysinline {
  %high1 = shufflevector <16 x i8> %v1, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %high2 = shufflevector <16 x i8> %v2, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %extended1 = zext <8 x i8> %high1 to <8 x i16>
  %extended2 = zext <8 x i8> %high2 to <8 x i16>
  %a = mul <8 x i16> %extended1, %extended2
  ret <8 x i16> %a
}

define weak_odr <16 x i16> @widening_mul_u8x16(<16 x i8> %x, <16 x i8> %y) nounwind alwaysinline {
  %1 = tail call <8 x i16> @extmul_low_u_v8i16(<16 x i8> %x, <16 x i8> %y)
  %2 = tail call <8 x i16> @extmul_high_u_v8i16(<16 x i8> %x, <16 x i8> %y)
  %3 = shufflevector <8 x i16> %1, <8 x i16> %2, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x i16> %3
}

; u16 -> u32
define weak_odr <4 x i32> @extmul_low_u_v4i32(<8 x i16> %v1, <8 x i16> %v2) nounwind alwaysinline {
  %low1 = shufflevector <8 x i16> %v1, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %low2 = shufflevector <8 x i16> %v2, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %extended1 = zext <4 x i16> %low1 to <4 x i32>
  %extended2 = zext <4 x i16> %low2 to <4 x i32>
  %a = mul <4 x i32> %extended1, %extended2
  ret <4 x i32> %a
}

define weak_odr <4 x i32> @extmul_high_u_v4i32(<8 x i16> %v1, <8 x i16> %v2) nounwind alwaysinline {
  %high1 = shufflevector <8 x i16> %v1, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %high2 = shufflevector <8 x i16> %v2, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %extended1 = zext <4 x i16> %high1 to <4 x i32>
  %extended2 = zext <4 x i16> %high2 to <4 x i32>
  %a = mul <4 x i32> %extended1, %extended2
  ret <4 x i32> %a
}

define weak_odr <8 x i32> @widening_mul_u16x8(<8 x i16> %x, <8 x i16> %y) nounwind alwaysinline {
  %1 = tail call <4 x i32> @extmul_low_u_v4i32(<8 x i16> %x, <8 x i16> %y)
  %2 = tail call <4 x i32> @extmul_high_u_v4i32(<8 x i16> %x, <8 x i16> %y)
  %3 = shufflevector <4 x i32> %1, <4 x i32> %2, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i32> %3
}

; u32 -> u64
define weak_odr <2 x i64> @extmul_low_u_v2i64(<4 x i32> %v1, <4 x i32> %v2) nounwind alwaysinline {
  %low1 = shufflevector <4 x i32> %v1, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %low2 = shufflevector <4 x i32> %v2, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %extended1 = zext <2 x i32> %low1 to <2 x i64>
  %extended2 = zext <2 x i32> %low2 to <2 x i64>
  %a = mul <2 x i64> %extended1, %extended2
  ret <2 x i64> %a
}

define weak_odr <2 x i64> @extmul_high_u_v2i64(<4 x i32> %v1, <4 x i32> %v2) nounwind alwaysinline {
  %high1 = shufflevector <4 x i32> %v1, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %high2 = shufflevector <4 x i32> %v2, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %extended1 = zext <2 x i32> %high1 to <2 x i64>
  %extended2 = zext <2 x i32> %high2 to <2 x i64>
  %a = mul <2 x i64> %extended1, %extended2
  ret <2 x i64> %a
}

define weak_odr <4 x i64> @widening_mul_u32x4(<4 x i32> %x, <4 x i32> %y) nounwind alwaysinline {
  %1 = tail call <2 x i64> @extmul_low_u_v2i64(<4 x i32> %x, <4 x i32> %y)
  %2 = tail call <2 x i64> @extmul_high_u_v2i64(<4 x i32> %x, <4 x i32> %y)
  %3 = shufflevector <2 x i64> %1, <2 x i64> %2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i64> %3
}

; saturating_narrow

declare <16 x i8> @llvm.wasm.narrow.signed.v16i8.v8i16(<8 x i16>, <8 x i16>)
declare <16 x i8> @llvm.wasm.narrow.unsigned.v16i8.v8i16(<8 x i16>, <8 x i16>)
declare <8 x i16> @llvm.wasm.narrow.signed.v8i16.v4i32(<4 x i32>, <4 x i32>)
declare <8 x i16> @llvm.wasm.narrow.unsigned.v8i16.v4i32(<4 x i32>, <4 x i32>)

define weak_odr <16 x i8> @saturating_narrow_i16x16_to_i8x16(<16 x i16> %x) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.wasm.narrow.signed.v16i8.v8i16(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <16 x i8> @saturating_narrow_i16x16_to_u8x16(<16 x i16> %x) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.wasm.narrow.unsigned.v16i8.v8i16(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <8 x i16> @saturating_narrow_i32x8_to_i16x8(<8 x i32> %x) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.wasm.narrow.signed.v8i16.v4i32(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}

define weak_odr <8 x i16> @saturating_narrow_i32x8_to_u16x8(<8 x i32> %x) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.wasm.narrow.unsigned.v8i16.v4i32(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}

; single to double-precision floating point (only needed for LLVM_VERSION == 13)
define weak_odr <4 x double> @float_to_double(<4 x float> %x) nounwind alwaysinline {
  %1 = fpext <4 x float> %x to <4 x double>
  ret <4 x double> %1
}

; Integer to integer extension

; i8 -> i16

define weak_odr <16 x i16> @extend_i8x16_to_i16x8(<16 x i8> %x) nounwind alwaysinline {
  %1 = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = sext <8 x i8> %1 to <8 x i16>
  %4 = sext <8 x i8> %2 to <8 x i16>
  %5 = shufflevector <8 x i16> %3, <8 x i16> %4, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x i16> %5
}

; u8 -> u16

define weak_odr <16 x i16> @extend_u8x16_to_u16x8(<16 x i8> %x) nounwind alwaysinline {
  %1 = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = zext <8 x i8> %1 to <8 x i16>
  %4 = zext <8 x i8> %2 to <8 x i16>
  %5 = shufflevector <8 x i16> %3, <8 x i16> %4, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x i16> %5
}

; i16 -> i32

define weak_odr <8 x i32> @extend_i16x8_to_i32x8(<8 x i16> %x) nounwind alwaysinline {
  %1 = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %3 = sext <4 x i16> %1 to <4 x i32>
  %4 = sext <4 x i16> %2 to <4 x i32>
  %5 = shufflevector <4 x i32> %3, <4 x i32> %4, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i32> %5
}

; u16 -> u32

define weak_odr <8 x i32> @extend_u16x8_to_u32x8(<8 x i16> %x) nounwind alwaysinline {
  %1 = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %3 = zext <4 x i16> %1 to <4 x i32>
  %4 = zext <4 x i16> %2 to <4 x i32>
  %5 = shufflevector <4 x i32> %3, <4 x i32> %4, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i32> %5
}

; i32 -> i64

define weak_odr <4 x i64> @extend_i32x4_to_i64x4(<4 x i32> %x) nounwind alwaysinline {
  %1 = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %2 = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %3 = sext <2 x i32> %1 to <2 x i64>
  %4 = sext <2 x i32> %2 to <2 x i64>
  %5 = shufflevector <2 x i64> %3, <2 x i64> %4, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i64> %5
}

; u32 -> u64

define weak_odr <4 x i64> @extend_u32x4_to_u64x4(<4 x i32> %x) nounwind alwaysinline {
  %1 = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %2 = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
  %3 = zext <2 x i32> %1 to <2 x i64>
  %4 = zext <2 x i32> %2 to <2 x i64>
  %5 = shufflevector <2 x i64> %3, <2 x i64> %4, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i64> %5
}




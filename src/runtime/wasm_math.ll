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

declare <2 x i64> @llvm.wasm.extmul.high.signed.v2i64(<4 x i32>, <4 x i32>);
declare <2 x i64> @llvm.wasm.extmul.low.signed.v2i64(<4 x i32>, <4 x i32>);
declare <2 x u64> @llvm.wasm.extmul.high.signed.v2u64(<4 x u32>, <4 x u32>);
declare <2 x u64> @llvm.wasm.extmul.low.signed.v2u64(<4 x u32>, <4 x u32>);
declare <4 x i32> @llvm.wasm.extmul.high.signed.v4i32(<8 x i16>, <8 x i16>);
declare <4 x i32> @llvm.wasm.extmul.low.signed.v4i32(<8 x i16>, <8 x i16>);
declare <4 x u32> @llvm.wasm.extmul.high.signed.v4u32(<8 x u16>, <8 x u16>);
declare <4 x u32> @llvm.wasm.extmul.low.signed.v4u32(<8 x u16>, <8 x u16>);
declare <8 x i16> @llvm.wasm.extmul.high.signed.v8i16(<16 x i8>, <16 x i8>);
declare <8 x i16> @llvm.wasm.extmul.low.signed.v8i16(<16 x i8>, <16 x i8>);
declare <8 x u16> @llvm.wasm.extmul.high.signed.v8u16(<16 x u8>, <16 x u8>);
declare <8 x u16> @llvm.wasm.extmul.low.signed.v8u16(<16 x u8>, <16 x u8>);

; i8 -> i16
define weak_odr <8 x i16> @widening_mul_i8x8(<8 x i8> %x, <8 x i8> %y) nounwind alwaysinline {
  %1 = shufflevector <8 x i8> %x, <8 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  %2 = shufflevector <8 x i8> %y, <8 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  %3 = tail call <8 x i16> @llvm.wasm.extmul.low.signed.v8i16(<16 x i8> %1, <16 x i8> %2)
  ret <8 x i16> %3
}

define weak_odr <16 x i16> @widening_mul_i8x16(<16 x i8> %x, <16 x i8> %y) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.wasm.extmul.low.signed.v8i16(<16 x i8> %x, <16 x i8> %y)
  %2 = tail call <8 x i16> @llvm.wasm.extmul.high.signed.v8i16(<16 x i8> %x, <16 x i8> %y)
  %3 = shufflevector <8 x i16> %1, <8 x i16> %2, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x i16> %3
}

; i16 -> i32
define weak_odr <4 x i32> @widening_mul_i16x4(<4 x i16> %x, <4 x i16> %y) nounwind alwaysinline {
  %1 = shufflevector <4 x i16> %x, <4 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  %2 = shufflevector <4 x i16> %y, <4 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  %3 = tail call <4 x i32> @llvm.wasm.extmul.low.signed.v4i32(<8 x i16> %1, <8 x i16> %2)
  ret <4 x i32> %3
}

define weak_odr <8 x i32> @widening_mul_i16x8(<8 x i16> %x, <8 x i16> %y) nounwind alwaysinline {
  %1 = tail call <4 x i32> @llvm.wasm.extmul.low.signed.v4i32(<8 x i16> %x, <8 x i16> %y)
  %2 = tail call <4 x i32> @llvm.wasm.extmul.high.signed.v4i32(<8 x i16> %x, <8 x i16> %y)
  %3 = shufflevector <4 x i32> %1, <4 x i32> %2, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x i32> %3
}

; i32 -> i64
define weak_odr <2 x i64> @widening_mul_i32x2(<2 x i32> %x, <2 x i32> %y) nounwind alwaysinline {
  %1 = shufflevector <2 x i32> %x, <2 x i32> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %2 = shufflevector <2 x i32> %y, <2 x i32> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %3 = tail call <2 x i64> @llvm.wasm.extmul.low.signed.v2i64(<4 x i32> %1, <4 x i32> %2)
  ret <2 x i64> %3
}

define weak_odr <4 x i64> @widening_mul_i32x4(<4 x i32> %x, <4 x i32> %y) nounwind alwaysinline {
  %1 = tail call <2 x i64> @llvm.wasm.extmul.low.signed.v2i64(<4 x i32> %x, <4 x i32> %y)
  %2 = tail call <2 x i64> @llvm.wasm.extmul.high.signed.v2i64(<4 x i32> %x, <4 x i32> %y)
  %3 = shufflevector <2 x i64> %1, <2 x i64> %2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i64> %3
}

; u8 -> u16
define weak_odr <8 x u16> @widening_mul_u8x8(<8 x u8> %x, <8 x u8> %y) nounwind alwaysinline {
  %1 = shufflevector <8 x u8> %x, <8 x u8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  %2 = shufflevector <8 x u8> %y, <8 x u8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef, i32 undef>
  %3 = tail call <8 x u16> @llvm.wasm.extmul.low.signed.v8u16(<16 x u8> %1, <16 x u8> %2)
  ret <8 x u16> %3
}

define weak_odr <16 x u16> @widening_mul_u8x16(<16 x u8> %x, <16 x u8> %y) nounwind alwaysinline {
  %1 = tail call <8 x u16> @llvm.wasm.extmul.low.signed.v8u16(<16 x u8> %x, <16 x u8> %y)
  %2 = tail call <8 x u16> @llvm.wasm.extmul.high.signed.v8u16(<16 x u8> %x, <16 x u8> %y)
  %3 = shufflevector <8 x u16> %1, <8 x u16> %2, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  ret <16 x u16> %3
}

; u16 -> u32
define weak_odr <4 x u32> @widening_mul_u16x4(<4 x u16> %x, <4 x u16> %y) nounwind alwaysinline {
  %1 = shufflevector <4 x u16> %x, <4 x u16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  %2 = shufflevector <4 x u16> %y, <4 x u16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 undef, i32 undef, i32 undef, i32 undef>
  %3 = tail call <4 x u32> @llvm.wasm.extmul.low.signed.v4u32(<8 x u16> %1, <8 x u16> %2)
  ret <4 x u32> %3
}

define weak_odr <8 x u32> @widening_mul_u16x8(<8 x u16> %x, <8 x u16> %y) nounwind alwaysinline {
  %1 = tail call <4 x u32> @llvm.wasm.extmul.low.signed.v4u32(<8 x u16> %x, <8 x u16> %y)
  %2 = tail call <4 x u32> @llvm.wasm.extmul.high.signed.v4u32(<8 x u16> %x, <8 x u16> %y)
  %3 = shufflevector <4 x u32> %1, <4 x u32> %2, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  ret <8 x u32> %3
}

; u32 -> u64
define weak_odr <2 x u64> @widening_mul_u32x2(<2 x u32> %x, <2 x u32> %y) nounwind alwaysinline {
  %1 = shufflevector <2 x u32> %x, <2 x u32> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %2 = shufflevector <2 x u32> %y, <2 x u32> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %3 = tail call <2 x u64> @llvm.wasm.extmul.low.signed.v2u64(<4 x u32> %1, <4 x u32> %2)
  ret <2 x u64> %3
}

define weak_odr <4 x u64> @widening_mul_u32x4(<4 x u32> %x, <4 x u32> %y) nounwind alwaysinline {
  %1 = tail call <2 x u64> @llvm.wasm.extmul.low.signed.v2u64(<4 x u32> %x, <4 x u32> %y)
  %2 = tail call <2 x u64> @llvm.wasm.extmul.high.signed.v2u64(<4 x u32> %x, <4 x u32> %y)
  %3 = shufflevector <2 x u64> %1, <2 x u64> %2, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x u64> %3
}



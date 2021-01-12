; Widening absolute difference ops. llvm peephole recognizes vabdl and
; vabal as calls to vabd followed by widening. Regardless of the
; signedness of the arg, these always zero-extend, because an absolute
; difference is always positive and may overflow a signed int.

declare <8 x i8> @llvm.aarch64.neon.sabd.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.uabd.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.sabd.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.uabd.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.sabd.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.uabd.v2i32(<2 x i32>, <2 x i32>) nounwind readnone

define weak_odr <8 x i16> @vabdl_i8x8(<8 x i8> %a, <8 x i8> %b) nounwind alwaysinline {
       %1 = call <8 x i8> @llvm.aarch64.neon.sabd.v8i8(<8 x i8> %a, <8 x i8> %b)
       %2 = zext <8 x i8> %1 to <8 x i16>
       ret <8 x i16> %2
}

define weak_odr <8 x i16> @vabdl_u8x8(<8 x i8> %a, <8 x i8> %b) nounwind alwaysinline {
       %1 = call <8 x i8> @llvm.aarch64.neon.uabd.v8i8(<8 x i8> %a, <8 x i8> %b)
       %2 = zext <8 x i8> %1 to <8 x i16>
       ret <8 x i16> %2
}

define weak_odr <4 x i32> @vabdl_i16x4(<4 x i16> %a, <4 x i16> %b) nounwind alwaysinline {
       %1 = call <4 x i16> @llvm.aarch64.neon.sabd.v4i16(<4 x i16> %a, <4 x i16> %b)
       %2 = zext <4 x i16> %1 to <4 x i32>
       ret <4 x i32> %2
}

define weak_odr <4 x i32> @vabdl_u16x4(<4 x i16> %a, <4 x i16> %b) nounwind alwaysinline {
       %1 = call <4 x i16> @llvm.aarch64.neon.uabd.v4i16(<4 x i16> %a, <4 x i16> %b)
       %2 = zext <4 x i16> %1 to <4 x i32>
       ret <4 x i32> %2
}

define weak_odr <2 x i64> @vabdl_i32x2(<2 x i32> %a, <2 x i32> %b) nounwind alwaysinline {
       %1 = call <2 x i32> @llvm.aarch64.neon.sabd.v2i32(<2 x i32> %a, <2 x i32> %b)
       %2 = zext <2 x i32> %1 to <2 x i64>
       ret <2 x i64> %2
}

define weak_odr <2 x i64> @vabdl_u32x2(<2 x i32> %a, <2 x i32> %b) nounwind alwaysinline {
       %1 = call <2 x i32> @llvm.aarch64.neon.uabd.v2i32(<2 x i32> %a, <2 x i32> %b)
       %2 = zext <2 x i32> %1 to <2 x i64>
       ret <2 x i64> %2
}

declare <4 x float> @llvm.aarch64.neon.frecpe.v4f32(<4 x float> %x) nounwind readnone;
declare <2 x float> @llvm.aarch64.neon.frecpe.v2f32(<2 x float> %x) nounwind readnone;
declare <4 x float> @llvm.aarch64.neon.frsqrte.v4f32(<4 x float> %x) nounwind readnone;
declare <2 x float> @llvm.aarch64.neon.frsqrte.v2f32(<2 x float> %x) nounwind readnone;
declare <4 x float> @llvm.aarch64.neon.frecps.v4f32(<4 x float> %x, <4 x float> %y) nounwind readnone;
declare <2 x float> @llvm.aarch64.neon.frecps.v2f32(<2 x float> %x, <2 x float> %y) nounwind readnone;
declare <4 x float> @llvm.aarch64.neon.frsqrts.v4f32(<4 x float> %x, <4 x float> %y) nounwind readnone;
declare <2 x float> @llvm.aarch64.neon.frsqrts.v2f32(<2 x float> %x, <2 x float> %y) nounwind readnone;

define weak_odr float @fast_inverse_f32(float %x) nounwind alwaysinline {
       %vec = insertelement <2 x float> undef, float %x, i32 0
       %approx = tail call <2 x float> @fast_inverse_f32x2(<2 x float> %vec)
       %result = extractelement <2 x float> %approx, i32 0
       ret float %result
}

define weak_odr <2 x float> @fast_inverse_f32x2(<2 x float> %x) nounwind alwaysinline {
       %approx = tail call <2 x float> @llvm.aarch64.neon.frecpe.v2f32(<2 x float> %x)
       %correction = tail call <2 x float> @llvm.aarch64.neon.frecps.v2f32(<2 x float> %approx, <2 x float> %x)
       %result = fmul <2 x float> %approx, %correction
       ret <2 x float> %result
}

define weak_odr <4 x float> @fast_inverse_f32x4(<4 x float> %x) nounwind alwaysinline {
       %approx = tail call <4 x float> @llvm.aarch64.neon.frecpe.v4f32(<4 x float> %x)
       %correction = tail call <4 x float> @llvm.aarch64.neon.frecps.v4f32(<4 x float> %approx, <4 x float> %x)
       %result = fmul <4 x float> %approx, %correction
       ret <4 x float> %result
}

define weak_odr float @fast_inverse_sqrt_f32(float %x) nounwind alwaysinline {
       %vec = insertelement <2 x float> undef, float %x, i32 0
       %approx = tail call <2 x float> @fast_inverse_sqrt_f32x2(<2 x float> %vec)
       %result = extractelement <2 x float> %approx, i32 0
       ret float %result
}

define weak_odr <2 x float> @fast_inverse_sqrt_f32x2(<2 x float> %x) nounwind alwaysinline {
       %approx = tail call <2 x float> @llvm.aarch64.neon.frsqrte.v2f32(<2 x float> %x)
       %approx2 = fmul <2 x float> %approx, %approx
       %correction = tail call <2 x float> @llvm.aarch64.neon.frsqrts.v2f32(<2 x float> %approx2, <2 x float> %x)
       %result = fmul <2 x float> %approx, %correction
       ret <2 x float> %result
}

define weak_odr <4 x float> @fast_inverse_sqrt_f32x4(<4 x float> %x) nounwind alwaysinline {
       %approx = tail call <4 x float> @llvm.aarch64.neon.frsqrte.v4f32(<4 x float> %x)
       %approx2 = fmul <4 x float> %approx, %approx
       %correction = tail call <4 x float> @llvm.aarch64.neon.frsqrts.v4f32(<4 x float> %approx2, <4 x float> %x)
       %result = fmul <4 x float> %approx, %correction
       ret <4 x float> %result
}

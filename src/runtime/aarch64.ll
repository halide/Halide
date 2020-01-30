; Absolute value ops


declare <4 x float> @llvm.fabs.v4f32(<4 x float>) nounwind readnone
declare <2 x float> @llvm.fabs.v2f32(<2 x float>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.abs.v4i32(<4 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.abs.v2i32(<2 x i32>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.abs.v4i16(<4 x i16>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.abs.v8i16(<8 x i16>) nounwind readnone
declare <8 x i8>  @llvm.aarch64.neon.abs.v8i8(<8 x i8>)   nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.abs.v16i8(<16 x i8>) nounwind readnone

define weak_odr <4 x float> @abs_f32x4(<4 x float> %x) nounwind alwaysinline {
       %tmp = call <4 x float> @llvm.fabs.v4f32(<4 x float> %x)
       ret <4 x float> %tmp
}

define weak_odr <2 x float> @abs_f32x2(<2 x float> %x) nounwind alwaysinline {
       %tmp = call <2 x float> @llvm.fabs.v2f32(<2 x float> %x)
       ret <2 x float> %tmp
}

define weak_odr <4 x i32> @abs_i32x4(<4 x i32> %x) nounwind alwaysinline {
       %tmp = call <4 x i32> @llvm.aarch64.neon.abs.v4i32(<4 x i32> %x)
       ret <4 x i32> %tmp
}

define weak_odr <2 x i32> @abs_i32x2(<2 x i32> %x) nounwind alwaysinline {
       %tmp = call <2 x i32> @llvm.aarch64.neon.abs.v2i32(<2 x i32> %x)
       ret <2 x i32> %tmp
}

define weak_odr <4 x i16> @abs_i16x4(<4 x i16> %x) nounwind alwaysinline {
       %tmp = call <4 x i16> @llvm.aarch64.neon.abs.v4i16(<4 x i16> %x)
       ret <4 x i16> %tmp
}

define weak_odr <8 x i16> @abs_i16x8(<8 x i16> %x) nounwind alwaysinline {
       %tmp = call <8 x i16> @llvm.aarch64.neon.abs.v8i16(<8 x i16> %x)
       ret <8 x i16> %tmp
}

define weak_odr <8 x i8> @abs_i8x8(<8 x i8> %x) nounwind alwaysinline {
       %tmp = call <8 x i8> @llvm.aarch64.neon.abs.v8i8(<8 x i8> %x)
       ret <8 x i8> %tmp
}

define weak_odr <16 x i8> @abs_i8x16(<16 x i8> %x) nounwind alwaysinline {
       %tmp = call <16 x i8> @llvm.aarch64.neon.abs.v16i8(<16 x i8> %x)
       ret <16 x i8> %tmp
}

declare <8 x i8> @llvm.aarch64.neon.sabd.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.uabd.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.sabd.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.uabd.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.sabd.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.uabd.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.sabd.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.uabd.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.sabd.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.uabd.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.sabd.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.uabd.v4i32(<4 x i32>, <4 x i32>) nounwind readnone

; Absolute difference ops

define weak_odr <4 x i32> @absd_i32x4(<4 x i32> %a, <4 x i32> %b) nounwind alwaysinline {
       %tmp = call <4 x i32> @llvm.aarch64.neon.sabd.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %tmp
}

define weak_odr <2 x i32> @absd_i32x2(<2 x i32> %a, <2 x i32> %b) nounwind alwaysinline {
       %tmp = call <2 x i32> @llvm.aarch64.neon.sabd.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %tmp
}

define weak_odr <4 x i16> @absd_i16x4(<4 x i16> %a, <4 x i16> %b) nounwind alwaysinline {
       %tmp = call <4 x i16> @llvm.aarch64.neon.sabd.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %tmp
}

define weak_odr <8 x i16> @absd_i16x8(<8 x i16> %a, <8 x i16> %b) nounwind alwaysinline {
       %tmp = call <8 x i16> @llvm.aarch64.neon.sabd.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %tmp
}

define weak_odr <8 x i8> @absd_i8x8(<8 x i8> %a, <8 x i8> %b) nounwind alwaysinline {
       %tmp = call <8 x i8> @llvm.aarch64.neon.sabd.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %tmp
}

define weak_odr <16 x i8> @absd_i8x16(<16 x i8> %a, <16 x i8> %b) nounwind alwaysinline {
       %tmp = call <16 x i8> @llvm.aarch64.neon.sabd.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %tmp
}

define weak_odr <4 x i32> @absd_u32x4(<4 x i32> %a, <4 x i32> %b) nounwind alwaysinline {
       %tmp = call <4 x i32> @llvm.aarch64.neon.uabd.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %tmp
}

define weak_odr <2 x i32> @absd_u32x2(<2 x i32> %a, <2 x i32> %b) nounwind alwaysinline {
       %tmp = call <2 x i32> @llvm.aarch64.neon.uabd.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %tmp
}

define weak_odr <4 x i16> @absd_u16x4(<4 x i16> %a, <4 x i16> %b) nounwind alwaysinline {
       %tmp = call <4 x i16> @llvm.aarch64.neon.uabd.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %tmp
}

define weak_odr <8 x i16> @absd_u16x8(<8 x i16> %a, <8 x i16> %b) nounwind alwaysinline {
       %tmp = call <8 x i16> @llvm.aarch64.neon.uabd.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %tmp
}

define weak_odr <8 x i8> @absd_u8x8(<8 x i8> %a, <8 x i8> %b) nounwind alwaysinline {
       %tmp = call <8 x i8> @llvm.aarch64.neon.uabd.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %tmp
}

define weak_odr <16 x i8> @absd_u8x16(<16 x i8> %a, <16 x i8> %b) nounwind alwaysinline {
       %tmp = call <16 x i8> @llvm.aarch64.neon.uabd.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %tmp
}

; Widening absolute difference ops. llvm peephole recognizes vabdl and
; vabal as calls to vabd followed by widening. Regardless of the
; signedness of the arg, these always zero-extend, because an absolute
; difference is always positive and may overflow a signed int.

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

declare <4 x float> @llvm.sqrt.v4f32(<4 x float>);
declare <2 x double> @llvm.sqrt.v2f64(<2 x double>);

define weak_odr <4 x float> @sqrt_f32x4(<4 x float> %x) nounwind alwaysinline {
       %tmp = call <4 x float> @llvm.sqrt.v4f32(<4 x float> %x)
       ret <4 x float> %tmp
}

define weak_odr <2 x double> @sqrt_f64x2(<2 x double> %x) nounwind alwaysinline {
       %tmp = call <2 x double> @llvm.sqrt.v2f64(<2 x double> %x)
       ret <2 x double> %tmp
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

; The way llvm represents intrinsics for horizontal addition are
; somewhat ad-hoc, and can be incompatible with the way we slice up
; intrinsics to meet the native vector width. We define wrappers for
; everything here instead.

declare <2 x double> @llvm.aarch64.neon.faddp.v2f64(<2 x double>, <2 x double>) nounwind readnone
declare <2 x float> @llvm.aarch64.neon.faddp.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.addp.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <2 x i64> @llvm.aarch64.neon.addp.v2i64(<2 x i64>, <2 x i64>) nounwind readnone
declare <4 x float> @llvm.aarch64.neon.faddp.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.addp.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.addp.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.addp.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.addp.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.addp.v16i8(<16 x i8>, <16 x i8>) nounwind readnone

define weak_odr <8 x i8> @pairwise_Add_int8x8_int8x16(<16 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i8> @llvm.aarch64.neon.addp.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %result
}

define weak_odr <16 x i8> @pairwise_Add_int8x16_int8x32(<32 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %b = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
       %result = tail call <16 x i8> @llvm.aarch64.neon.addp.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %result
}

define weak_odr <4 x i16> @pairwise_Add_int16x4_int16x8(<8 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i16> @llvm.aarch64.neon.addp.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %result
}

define weak_odr <8 x i16> @pairwise_Add_int16x8_int16x16(<16 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i16> @llvm.aarch64.neon.addp.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Add_int32x4_int32x8(<8 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i32> @llvm.aarch64.neon.addp.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Add_int32x2_int32x4(<4 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i32> @llvm.aarch64.neon.addp.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %result
}


define weak_odr <2 x i64> @pairwise_Add_int64x2_int64x4(<4 x i64> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i64> %x, <4 x i64> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i64> %x, <4 x i64> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i64> @llvm.aarch64.neon.addp.v2i64(<2 x i64> %a, <2 x i64> %b)
       ret <2 x i64> %result
}

define weak_odr <4 x float> @pairwise_Add_float32x4_float32x8(<8 x float> %x) nounwind alwaysinline {
       %a = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x float> @llvm.aarch64.neon.faddp.v4f32(<4 x float> %a, <4 x float> %b)
       ret <4 x float> %result
}

define weak_odr <2 x float> @pairwise_Add_float32x2_float32x4(<4 x float> %x) nounwind alwaysinline {
       %a = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x float> @llvm.aarch64.neon.faddp.v2f32(<2 x float> %a, <2 x float> %b)
       ret <2 x float> %result
}


define weak_odr <2 x double> @pairwise_Add_float64x4(<4 x double> %x) nounwind alwaysinline {
       %a = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x double> @llvm.aarch64.neon.faddp.v2f64(<2 x double> %a, <2 x double> %b)
       ret <2 x double> %result
}


declare <1 x i64> @llvm.aarch64.neon.saddlp.v1i64.v2i32(<2 x i32>) nounwind readnone
declare <1 x i64> @llvm.aarch64.neon.uaddlp.v1i64.v2i32(<2 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.saddlp.v2i32.v4i16(<4 x i16>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.uaddlp.v2i32.v4i16(<4 x i16>) nounwind readnone
declare <2 x i64> @llvm.aarch64.neon.saddlp.v2i64.v4i32(<4 x i32>) nounwind readnone
declare <2 x i64> @llvm.aarch64.neon.uaddlp.v2i64.v4i32(<4 x i32>) nounwind readnone
declare <4 x i16>  @llvm.aarch64.neon.saddlp.v4i16.v8i8(<8 x i8>) nounwind readnone
declare <4 x i16>  @llvm.aarch64.neon.uaddlp.v4i16.v8i8(<8 x i8>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.saddlp.v4i32.v8i16(<8 x i16>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.uaddlp.v4i32.v8i16(<8 x i16>) nounwind readnone
declare <8 x i16>  @llvm.aarch64.neon.saddlp.v8i16.v16i8(<16 x i8>) nounwind readnone
declare <8 x i16>  @llvm.aarch64.neon.uaddlp.v8i16.v16i8(<16 x i8>) nounwind readnone


define weak_odr <8 x i16> @pairwise_Add_int16x8_int8x16(<16 x i8> %x) nounwind alwaysinline {
       %result = tail call <8 x i16> @llvm.aarch64.neon.saddlp.v8i16.v16i8(<16 x i8> %x)
       ret <8 x i16> %result
}

define weak_odr <4 x i16> @pairwise_Add_int16x4_int8x8(<8 x i8> %x) nounwind alwaysinline {
       %result = tail call <4 x i16> @llvm.aarch64.neon.saddlp.v4i16.v8i8(<8 x i8> %x)
       ret <4 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Add_int32x4_int16x8(<8 x i16> %x) nounwind alwaysinline {
       %result = tail call <4 x i32> @llvm.aarch64.neon.saddlp.v4i32.v8i16(<8 x i16> %x)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Add_int32x2_int16x4(<4 x i16> %x) nounwind alwaysinline {
       %result = tail call <2 x i32> @llvm.aarch64.neon.saddlp.v2i32.v4i16(<4 x i16> %x)
       ret <2 x i32> %result
}

define weak_odr <2 x i64> @pairwise_Add_int64x2_int32x4(<4 x i32> %x) nounwind alwaysinline {
       %result = tail call <2 x i64> @llvm.aarch64.neon.saddlp.v2i64.v4i32(<4 x i32> %x)
       ret <2 x i64> %result
}

define weak_odr <1 x i64> @pairwise_Add_int64x1_int32x2(<2 x i32> %x) nounwind alwaysinline {
       %result = tail call <1 x i64> @llvm.aarch64.neon.saddlp.v1i64.v2i32(<2 x i32> %x)
       ret <1 x i64> %result
}

define weak_odr <8 x i16> @pairwise_Add_uint16x8_uint8x16(<16 x i8> %x) nounwind alwaysinline {
       %result = tail call <8 x i16> @llvm.aarch64.neon.uaddlp.v8i16.v16i8(<16 x i8> %x)
       ret <8 x i16> %result
}

define weak_odr <4 x i16> @pairwise_Add_uint16x4_uint8x8(<8 x i8> %x) nounwind alwaysinline {
       %result = tail call <4 x i16> @llvm.aarch64.neon.uaddlp.v4i16.v8i8(<8 x i8> %x)
       ret <4 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Add_uint32x4_uint16x8(<8 x i16> %x) nounwind alwaysinline {
       %result = tail call <4 x i32> @llvm.aarch64.neon.uaddlp.v4i32.v8i16(<8 x i16> %x)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Add_uint32x2_uint16x4(<4 x i16> %x) nounwind alwaysinline {
       %result = tail call <2 x i32> @llvm.aarch64.neon.uaddlp.v2i32.v4i16(<4 x i16> %x)
       ret <2 x i32> %result
}

define weak_odr <2 x i64> @pairwise_Add_uint64x2_uint32x4(<4 x i32> %x) nounwind alwaysinline {
       %result = tail call <2 x i64> @llvm.aarch64.neon.uaddlp.v2i64.v4i32(<4 x i32> %x)
       ret <2 x i64> %result
}

define weak_odr <1 x i64> @pairwise_Add_uint64x1_uint32x2(<2 x i32> %x) nounwind alwaysinline {
       %result = tail call <1 x i64> @llvm.aarch64.neon.uaddlp.v1i64.v2i32(<2 x i32> %x)
       ret <1 x i64> %result
}

declare <16 x i8> @llvm.aarch64.neon.smaxp.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.umaxp.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <2 x double> @llvm.aarch64.neon.fmaxp.v2f64(<2 x double>, <2 x double>) nounwind readnone
declare <2 x float> @llvm.aarch64.neon.fmaxp.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.smaxp.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.umaxp.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <4 x float> @llvm.aarch64.neon.fmaxp.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.smaxp.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.umaxp.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.smaxp.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.umaxp.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.smaxp.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.umaxp.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.smaxp.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.umaxp.v8i8(<8 x i8>, <8 x i8>) nounwind readnone

define weak_odr <8 x i8> @pairwise_Max_int8x8_int8x16(<16 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i8> @llvm.aarch64.neon.smaxp.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %result
}

define weak_odr <16 x i8> @pairwise_Max_int8x16_int8x32(<32 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %b = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
       %result = tail call <16 x i8> @llvm.aarch64.neon.smaxp.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %result
}

define weak_odr <4 x i16> @pairwise_Max_int16x4_int16x8(<8 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i16> @llvm.aarch64.neon.smaxp.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %result
}

define weak_odr <8 x i16> @pairwise_Max_int16x8_int16x16(<16 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i16> @llvm.aarch64.neon.smaxp.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Max_int32x4_int32x8(<8 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i32> @llvm.aarch64.neon.smaxp.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Max_int32x2_int32x4(<4 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i32> @llvm.aarch64.neon.smaxp.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %result
}

define weak_odr <4 x float> @pairwise_Max_float32x4_float32x8(<8 x float> %x) nounwind alwaysinline {
       %a = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x float> @llvm.aarch64.neon.fmaxp.v4f32(<4 x float> %a, <4 x float> %b)
       ret <4 x float> %result
}

define weak_odr <2 x float> @pairwise_Max_float32x2_float32x4(<4 x float> %x) nounwind alwaysinline {
       %a = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x float> @llvm.aarch64.neon.fmaxp.v2f32(<2 x float> %a, <2 x float> %b)
       ret <2 x float> %result
}


define weak_odr <2 x double> @pairwise_Max_float64x4(<4 x double> %x) nounwind alwaysinline {
       %a = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x double> @llvm.aarch64.neon.fmaxp.v2f64(<2 x double> %a, <2 x double> %b)
       ret <2 x double> %result
}


define weak_odr <8 x i8> @pairwise_Max_uint8x8_uint8x16(<16 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i8> @llvm.aarch64.neon.umaxp.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %result
}

define weak_odr <16 x i8> @pairwise_Max_uint8x16_uint8x32(<32 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %b = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
       %result = tail call <16 x i8> @llvm.aarch64.neon.umaxp.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %result
}

define weak_odr <4 x i16> @pairwise_Max_uint16x4_uint16x8(<8 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i16> @llvm.aarch64.neon.umaxp.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %result
}

define weak_odr <8 x i16> @pairwise_Max_uint16x8_uint16x16(<16 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i16> @llvm.aarch64.neon.umaxp.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Max_uint32x4_uint32x8(<8 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i32> @llvm.aarch64.neon.umaxp.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Max_uint32x2_uint32x4(<4 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i32> @llvm.aarch64.neon.umaxp.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %result
}


declare <16 x i8> @llvm.aarch64.neon.sminp.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <16 x i8> @llvm.aarch64.neon.uminp.v16i8(<16 x i8>, <16 x i8>) nounwind readnone
declare <2 x double> @llvm.aarch64.neon.fminp.v2f64(<2 x double>, <2 x double>) nounwind readnone
declare <2 x float> @llvm.aarch64.neon.fminp.v2f32(<2 x float>, <2 x float>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.sminp.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <2 x i32> @llvm.aarch64.neon.uminp.v2i32(<2 x i32>, <2 x i32>) nounwind readnone
declare <4 x float> @llvm.aarch64.neon.fminp.v4f32(<4 x float>, <4 x float>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.sminp.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i16> @llvm.aarch64.neon.uminp.v4i16(<4 x i16>, <4 x i16>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.sminp.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <4 x i32> @llvm.aarch64.neon.uminp.v4i32(<4 x i32>, <4 x i32>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.sminp.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i16> @llvm.aarch64.neon.uminp.v8i16(<8 x i16>, <8 x i16>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.sminp.v8i8(<8 x i8>, <8 x i8>) nounwind readnone
declare <8 x i8> @llvm.aarch64.neon.uminp.v8i8(<8 x i8>, <8 x i8>) nounwind readnone

define weak_odr <8 x i8> @pairwise_Min_int8x8_int8x16(<16 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i8> @llvm.aarch64.neon.sminp.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %result
}

define weak_odr <16 x i8> @pairwise_Min_int8x16_int8x32(<32 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %b = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
       %result = tail call <16 x i8> @llvm.aarch64.neon.sminp.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %result
}

define weak_odr <4 x i16> @pairwise_Min_int16x4_int16x8(<8 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i16> @llvm.aarch64.neon.sminp.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %result
}

define weak_odr <8 x i16> @pairwise_Min_int16x8_int16x16(<16 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i16> @llvm.aarch64.neon.sminp.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Min_int32x4_int32x8(<8 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i32> @llvm.aarch64.neon.sminp.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Min_int32x2_int32x4(<4 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i32> @llvm.aarch64.neon.sminp.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %result
}


define weak_odr <4 x float> @pairwise_Min_float32x4_float32x8(<8 x float> %x) nounwind alwaysinline {
       %a = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x float> %x, <8 x float> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x float> @llvm.aarch64.neon.fminp.v4f32(<4 x float> %a, <4 x float> %b)
       ret <4 x float> %result
}

define weak_odr <2 x float> @pairwise_Min_float32x2_float32x4(<4 x float> %x) nounwind alwaysinline {
       %a = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x float> %x, <4 x float> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x float> @llvm.aarch64.neon.fminp.v2f32(<2 x float> %a, <2 x float> %b)
       ret <2 x float> %result
}


define weak_odr <2 x double> @pairwise_Min_float64x4(<4 x double> %x) nounwind alwaysinline {
       %a = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x double> %x, <4 x double> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x double> @llvm.aarch64.neon.fminp.v2f64(<2 x double> %a, <2 x double> %b)
       ret <2 x double> %result
}


define weak_odr <8 x i8> @pairwise_Min_uint8x8_uint8x16(<16 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i8> %x, <16 x i8> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i8> @llvm.aarch64.neon.uminp.v8i8(<8 x i8> %a, <8 x i8> %b)
       ret <8 x i8> %result
}

define weak_odr <16 x i8> @pairwise_Min_uint8x16_uint8x32(<32 x i8> %x) nounwind alwaysinline {
       %a = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %b = shufflevector <32 x i8> %x, <32 x i8> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
       %result = tail call <16 x i8> @llvm.aarch64.neon.uminp.v16i8(<16 x i8> %a, <16 x i8> %b)
       ret <16 x i8> %result
}

define weak_odr <4 x i16> @pairwise_Min_uint16x4_uint16x8(<8 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i16> %x, <8 x i16> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i16> @llvm.aarch64.neon.uminp.v4i16(<4 x i16> %a, <4 x i16> %b)
       ret <4 x i16> %result
}

define weak_odr <8 x i16> @pairwise_Min_uint16x8_uint16x16(<16 x i16> %x) nounwind alwaysinline {
       %a = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
       %b = shufflevector <16 x i16> %x, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
       %result = tail call <8 x i16> @llvm.aarch64.neon.uminp.v8i16(<8 x i16> %a, <8 x i16> %b)
       ret <8 x i16> %result
}

define weak_odr <4 x i32> @pairwise_Min_uint32x4_uint32x8(<8 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
       %b = shufflevector <8 x i32> %x, <8 x i32> undef, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
       %result = tail call <4 x i32> @llvm.aarch64.neon.uminp.v4i32(<4 x i32> %a, <4 x i32> %b)
       ret <4 x i32> %result
}

define weak_odr <2 x i32> @pairwise_Min_uint32x2_uint32x4(<4 x i32> %x) nounwind alwaysinline {
       %a = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
       %b = shufflevector <4 x i32> %x, <4 x i32> undef, <2 x i32> <i32 2, i32 3>
       %result = tail call <2 x i32> @llvm.aarch64.neon.uminp.v2i32(<2 x i32> %a, <2 x i32> %b)
       ret <2 x i32> %result
}

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

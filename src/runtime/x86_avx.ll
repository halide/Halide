; 256-bit native instructions for the following 6 instructions only
; come in with AVX2, but LLVM gets deeply confused if you slice up a
; 256-bit vector into 128-bit vectors, apply the narrower
; instructions, then reassemble. It prefers that you generate code at
; the AVX register width, and would rather handle the instruction
; legalization itself.

; Note that this is only used for LLVM 8.0+
define weak_odr <32 x i8> @paddusbx32(<32 x i8> %a0, <32 x i8> %a1) nounwind alwaysinline {
  %1 = add <32 x i8> %a0, %a1
  %2 = icmp ugt <32 x i8> %a0, %1
  %3 = select <32 x i1> %2, <32 x i8> <i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1, i8 -1>, <32 x i8> %1
  ret <32 x i8> %3
}

; Note that this is only used for LLVM 8.0+
define weak_odr <16 x i16> @padduswx16(<16 x i16> %a0, <16 x i16> %a1) nounwind alwaysinline {
  %1 = add <16 x i16> %a0, %a1
  %2 = icmp ugt <16 x i16> %a0, %1
  %3 = select <16 x i1> %2, <16 x i16> <i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1, i16 -1>, <16 x i16> %1
  ret <16 x i16> %3
}

; Note that this is only used for LLVM 8.0+
define weak_odr <32 x i8> @psubusbx32(<32 x i8> %a0, <32 x i8> %a1) nounwind alwaysinline {
  %1 = icmp ugt <32 x i8> %a0, %a1
  %2 = select <32 x i1> %1, <32 x i8> %a0, <32 x i8> %a1
  %3 = sub <32 x i8> %2, %a1
  ret <32 x i8> %3
}

; Note that this is only used for LLVM 8.0+
define weak_odr <16 x i16> @psubuswx16(<16 x i16> %a0, <16 x i16> %a1) nounwind alwaysinline {
  %1 = icmp ugt <16 x i16> %a0, %a1
  %2 = select <16 x i1> %1, <16 x i16> %a0, <16 x i16> %a1
  %3 = sub <16 x i16> %2, %a1
  ret <16 x i16> %3
}

; Note that this is only used for LLVM 6.0+
define weak_odr <32 x i8>  @pavgbx32(<32 x i8> %a, <32 x i8> %b) nounwind alwaysinline {
  %1 = zext <32 x i8> %a to <32 x i32>
  %2 = zext <32 x i8> %b to <32 x i32>
  %3 = add nuw nsw <32 x i32> %1, <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
  %4 = add nuw nsw <32 x i32> %3, %2
  %5 = lshr <32 x i32> %4, <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
  %6 = trunc <32 x i32> %5 to <32 x i8>
  ret <32 x i8> %6
}

; Note that this is only used for LLVM 6.0+
define weak_odr <16 x i16>  @pavgwx16(<16 x i16> %a, <16 x i16> %b) nounwind alwaysinline {
  %1 = zext <16 x i16> %a to <16 x i32>
  %2 = zext <16 x i16> %b to <16 x i32>
  %3 = add nuw nsw <16 x i32> %1, <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
  %4 = add nuw nsw <16 x i32> %3, %2
  %5 = lshr <16 x i32> %4, <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
  %6 = trunc <16 x i32> %5 to <16 x i16>
  ret <16 x i16> %6
}

declare <8 x float> @llvm.x86.avx.sqrt.ps.256(<8 x float>) nounwind readnone
declare <4 x double> @llvm.x86.avx.sqrt.pd.256(<4 x double>) nounwind readnone
declare <8 x float> @llvm.x86.avx.round.ps.256(<8 x float>, i32) nounwind readnone
declare <4 x double> @llvm.x86.avx.round.pd.256(<4 x double>, i32) nounwind readnone

define weak_odr <8 x float> @sqrt_f32x8(<8 x float> %arg) nounwind alwaysinline {
   %1 = tail call <8 x float> @llvm.x86.avx.sqrt.ps.256(<8 x float> %arg) nounwind
   ret <8 x float> %1
}

define weak_odr <4 x double> @sqrt_f64x4(<4 x double> %arg) nounwind alwaysinline {
   %1 = tail call <4 x double> @llvm.x86.avx.sqrt.pd.256(<4 x double> %arg) nounwind
   ret <4 x double> %1
}

define weak_odr <8 x float> @round_f32x8(<8 x float> %arg) nounwind alwaysinline {
   %1 = tail call <8 x float> @llvm.x86.avx.round.ps.256(<8 x float> %arg, i32 0) nounwind
   ret <8 x float> %1
}

define weak_odr <4 x double> @round_f64x4(<4 x double> %arg) nounwind alwaysinline {
   %1 = tail call <4 x double> @llvm.x86.avx.round.pd.256(<4 x double> %arg, i32 0) nounwind
   ret <4 x double> %1
}

define weak_odr <8 x float> @ceil_f32x8(<8 x float> %arg) nounwind alwaysinline {
   %1 = tail call <8 x float> @llvm.x86.avx.round.ps.256(<8 x float> %arg, i32 2) nounwind
   ret <8 x float> %1
}

define weak_odr <4 x double> @ceil_f64x4(<4 x double> %arg) nounwind alwaysinline {
   %1 = tail call <4 x double> @llvm.x86.avx.round.pd.256(<4 x double> %arg, i32 2) nounwind
   ret <4 x double> %1
}

define weak_odr <8 x float> @floor_f32x8(<8 x float> %arg) nounwind alwaysinline {
   %1 = tail call <8 x float> @llvm.x86.avx.round.ps.256(<8 x float> %arg, i32 1) nounwind
   ret <8 x float> %1
}

define weak_odr <4 x double> @floor_f64x4(<4 x double> %arg) nounwind alwaysinline {
   %1 = tail call <4 x double> @llvm.x86.avx.round.pd.256(<4 x double> %arg, i32 1) nounwind
   ret <4 x double> %1
}

define weak_odr <8 x float> @trunc_f32x8(<8 x float> %arg) nounwind alwaysinline {
   %1 = tail call <8 x float> @llvm.x86.avx.round.ps.256(<8 x float> %arg, i32 3) nounwind
   ret <8 x float> %1
}

define weak_odr <4 x double> @trunc_f64x4(<4 x double> %arg) nounwind alwaysinline {
   %1 = tail call <4 x double> @llvm.x86.avx.round.pd.256(<4 x double> %arg, i32 3) nounwind
   ret <4 x double> %1
}

define weak_odr <8 x float> @abs_f32x8(<8 x float> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <8 x float> %x to <8 x i32>
  %mask = lshr <8 x i32> <i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1>, <i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>
  %masked = and <8 x i32> %arg, %mask
  %result = bitcast <8 x i32> %masked to <8 x float>
  ret <8 x float> %result
}

define weak_odr <4 x double> @abs_f64x4(<4 x double> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <4 x double> %x to <4 x i64>
  %mask = lshr <4 x i64> <i64 -1, i64 -1, i64 -1, i64 -1>, <i64 1, i64 1, i64 1, i64 1>
  %masked = and <4 x i64> %arg, %mask
  %result = bitcast <4 x i64> %masked to <4 x double>
  ret <4 x double> %result
}

declare <8 x float> @llvm.x86.avx.rcp.ps.256(<8 x float>) nounwind readnone

define weak_odr <8 x float> @fast_inverse_f32x8(<8 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <8 x float> @llvm.x86.avx.rcp.ps.256(<8 x float> %x);
  ret <8 x float> %approx
}

declare <8 x float> @llvm.x86.avx.rsqrt.ps.256(<8 x float>) nounwind readnone

define weak_odr <8 x float> @fast_inverse_sqrt_f32x8(<8 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <8 x float> @llvm.x86.avx.rsqrt.ps.256(<8 x float> %x);
  ret <8 x float> %approx
}

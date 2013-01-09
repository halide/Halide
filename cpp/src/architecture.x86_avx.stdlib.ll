
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


declare <16 x i8> @llvm.x86.sse2.packsswb.128(<8 x i16>, <8 x i16>)
declare <16 x i8> @llvm.x86.sse2.packuswb.128(<8 x i16>, <8 x i16>)
declare <8 x i16> @llvm.x86.sse2.packssdw.128(<4 x i32>, <4 x i32>)
declare <8 x i16> @llvm.x86.sse41.packusdw(<4 x i32>, <4 x i32>)

define weak_odr <16 x i8>  @packsswbx16(<16 x i16> %arg) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.x86.sse2.packsswb.128(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <16 x i8>  @packuswbx16(<16 x i16> %arg) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>
  %2 = shufflevector <16 x i16> %arg, <16 x i16> undef, <8 x i32> <i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %3 = tail call <16 x i8> @llvm.x86.sse2.packuswb.128(<8 x i16> %1, <8 x i16> %2)
  ret <16 x i8> %3
}

define weak_odr <8 x i16>  @packssdwx8(<8 x i32> %arg) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> < i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.x86.sse2.packssdw.128(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}

define weak_odr <8 x i16>  @packusdwx8(<8 x i32> %arg) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> < i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.x86.sse41.packusdw(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}
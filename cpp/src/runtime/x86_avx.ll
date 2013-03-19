
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


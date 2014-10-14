
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

define weak_odr <8 x float> @inverse_f32x8(<8 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <8 x float> @llvm.x86.avx.rcp.ps.256(<8 x float> %x);
  %prod = fmul <8 x float> %approx, %x
  %diff = fsub <8 x float> <float 2.0, float 2.0, float 2.0, float 2.0, float 2.0, float 2.0, float 2.0, float 2.0>, %prod
  %result = fmul <8 x float> %approx, %diff
  ret <8 x float> %result
}

declare <8 x float> @llvm.x86.avx.rsqrt.ps.256(<8 x float>) nounwind readnone

define weak_odr <8 x float> @inverse_sqrt_f32x8(<8 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <8 x float> @llvm.x86.avx.rsqrt.ps.256(<8 x float> %x);
  %prod = fmul <8 x float> %approx, %approx
  %prod2 = fmul <8 x float> %prod, %x
  %diff = fsub <8 x float> <float 3.0, float 3.0, float 3.0, float 3.0, float 3.0, float 3.0, float 3.0, float 3.0>, %prod2
  %scale = fmul <8 x float> <float 0.5, float 0.5, float 0.5, float 0.5,float 0.5, float 0.5, float 0.5, float 0.5 >, %diff
  %result = fmul <8 x float> %approx, %scale
  ret <8 x float> %result
}
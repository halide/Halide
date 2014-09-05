
declare <16 x i8> @llvm.x86.sse2.packsswb.128(<8 x i16>, <8 x i16>)
declare <16 x i8> @llvm.x86.sse2.packuswb.128(<8 x i16>, <8 x i16>)
declare <8 x i16> @llvm.x86.sse2.packssdw.128(<4 x i32>, <4 x i32>)

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

declare <4 x i32> @llvm.x86.sse2.pmadd.wd(<8 x i16>, <8 x i16>)

define weak_odr <4 x i32> @pmaddwdx4(<4 x i16> %a, <4 x i16> %b, <4 x i16> %c, <4 x i16> %d) nounwind alwaysinline {
  %1 = shufflevector <4 x i16> %a, <4 x i16> %c, <8 x i32> <i32 0, i32 4, i32 1, i32 5, i32 2, i32 6, i32 3, i32 7>
  %2 = shufflevector <4 x i16> %b, <4 x i16> %d, <8 x i32> <i32 0, i32 4, i32 1, i32 5, i32 2, i32 6, i32 3, i32 7>
  %3 = tail call <4 x i32> @llvm.x86.sse2.pmadd.wd(<8 x i16> %1, <8 x i16> %2)
  ret <4 x i32> %3
}

; TODO: Use vpmaddwd in avx2
define weak_odr <8 x i32> @pmaddwdx8(<8 x i16> %a, <8 x i16> %b, <8 x i16> %c, <8 x i16> %d) nounwind alwaysinline {
  %1 = shufflevector <8 x i16> %a, <8 x i16> %c, <8 x i32> <i32 0, i32 8, i32 1, i32 9, i32 2, i32 10, i32 3, i32 11>
  %2 = shufflevector <8 x i16> %b, <8 x i16> %d, <8 x i32> <i32 0, i32 8, i32 1, i32 9, i32 2, i32 10, i32 3, i32 11>
  %3 = tail call <4 x i32> @llvm.x86.sse2.pmadd.wd(<8 x i16> %1, <8 x i16> %2)

  %4 = shufflevector <8 x i16> %a, <8 x i16> %c, <8 x i32> <i32 4, i32 12, i32 5, i32 13, i32 6, i32 14, i32 7, i32 15>
  %5 = shufflevector <8 x i16> %b, <8 x i16> %d, <8 x i32> <i32 4, i32 12, i32 5, i32 13, i32 6, i32 14, i32 7, i32 15>
  %6 = tail call <4 x i32> @llvm.x86.sse2.pmadd.wd(<8 x i16> %4, <8 x i16> %5)

  %7 = shufflevector <4 x i32> %3, <4 x i32> %6, <8 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7>

  ret <8 x i32> %7
}

define weak_odr <4 x float> @sqrt_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float> %x) nounwind
  ret <4 x float> %1
}

declare <2 x double> @llvm.x86.sse2.sqrt.pd(<2 x double>) nounwind readnone

define weak_odr <2 x double> @sqrt_f64x2(<2 x double> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse2.sqrt.pd(<2 x double> %x) nounwind
  ret <2 x double> %1
}

declare <4 x float> @llvm.x86.sse.sqrt.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @abs_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <4 x float> %x to <4 x i32>
  %mask = lshr <4 x i32> <i32 -1, i32 -1, i32 -1, i32 -1>, <i32 1, i32 1, i32 1, i32 1>
  %masked = and <4 x i32> %arg, %mask
  %result = bitcast <4 x i32> %masked to <4 x float>
  ret <4 x float> %result
}

define weak_odr <2 x double> @abs_f64x2(<2 x double> %x) nounwind uwtable readnone alwaysinline {
  %arg = bitcast <2 x double> %x to <2 x i64>
  %mask = lshr <2 x i64> <i64 -1, i64 -1>, <i64 1, i64 1>
  %masked = and <2 x i64> %arg, %mask
  %result = bitcast <2 x i64> %masked to <2 x double>
  ret <2 x double> %result
}

declare <4 x float> @llvm.x86.sse.rcp.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @inverse_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.x86.sse.rcp.ps(<4 x float> %x);
  %prod = fmul <4 x float> %approx, %x
  %diff = fsub <4 x float> <float 2.0, float 2.0, float 2.0, float 2.0>, %prod
  %result = fmul <4 x float> %approx, %diff
  ret <4 x float> %result
}

declare <4 x float> @llvm.x86.sse.rsqrt.ps(<4 x float>) nounwind readnone

define weak_odr <4 x float> @inverse_sqrt_f32x4(<4 x float> %x) nounwind uwtable readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.x86.sse.rsqrt.ps(<4 x float> %x);
  %prod = fmul <4 x float> %approx, %approx
  %prod2 = fmul <4 x float> %prod, %x
  %diff = fsub <4 x float> <float 3.0, float 3.0, float 3.0, float 3.0>, %prod2
  %scale = fmul <4 x float> <float 0.5, float 0.5, float 0.5, float 0.5>, %diff
  %result = fmul <4 x float> %approx, %scale
  ret <4 x float> %result
}




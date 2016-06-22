declare <4 x float> @llvm.ppc.altivec.vrefp(<4 x float>) nounwind readnone
declare <4 x float> @llvm.ppc.altivec.vrsqrtefp(<4 x float>) nounwind readnone

define weak_odr float @fast_inverse_f32(float %x) readnone alwaysinline {
  %vec = insertelement <4 x float> undef, float %x, i32 0
  %approx = tail call <4 x float> @llvm.ppc.altivec.vrefp(<4 x float> %vec)
  %result = extractelement <4 x float> %approx, i32 0
  ret float %result
}

define weak_odr <4 x float> @fast_inverse_f32x4(<4 x float> %x) readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.ppc.altivec.vrefp(<4 x float> %x) #2
  ret <4 x float> %approx
}

define weak_odr float @fast_inverse_sqrt_f32(float %x) readnone alwaysinline {
  %vec = insertelement <4 x float> undef, float %x, i32 0
  %approx = tail call <4 x float> @llvm.ppc.altivec.vrsqrtefp(<4 x float> %vec)
  %result = extractelement <4 x float> %approx, i32 0
  ret float %result
}

define weak_odr <4 x float> @fast_inverse_sqrt_f32x4(<4 x float> %x) readnone alwaysinline {
  %approx = tail call <4 x float> @llvm.ppc.altivec.vrsqrtefp(<4 x float> %x) #2
  ret <4 x float> %approx
}

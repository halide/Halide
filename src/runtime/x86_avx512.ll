
; Split a 32 element f32 vector into two 16 element vectors to use the cvtne2ps2bf16 intrinsic.
define weak_odr <32 x i16>  @vcvtne2ps2bf16x32(<32 x float> %arg) nounwind alwaysinline {
  %1 = shufflevector <32 x float> %arg, <32 x float> undef, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %2 = shufflevector <32 x float> %arg, <32 x float> undef, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %3 = tail call <32 x i16> @llvm.x86.avx512bf16.cvtne2ps2bf16.512(<16 x float> %2, <16 x float> %1)
  ret <32 x i16> %3
}

declare <32 x i16> @llvm.x86.avx512bf16.cvtne2ps2bf16.512(<16 x float>, <16 x float>)

; LLVM does not have an unmasked version of cvtneps2bf16.128, so provide a wrapper around the masked version.
define weak_odr <4 x i16>  @vcvtneps2bf16x4(<4 x float> %arg) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.avx512bf16.mask.cvtneps2bf16.128(<4 x float> %arg, <8 x i16> undef, <4 x i1> <i1 true, i1 true, i1 true, i1 true>)
  %2 = shufflevector <8 x i16> %1, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i16> %2
}

declare <8 x i16> @llvm.x86.avx512bf16.mask.cvtneps2bf16.128(<4 x float>, <8 x i16>, <4 x i1>)


; LLVM does not have an unmasked version of cvtneps2bf16.128, so provide a wrapper around the masked version.
define weak_odr <4 x i16>  @vcvtneps2bf16x4(<4 x float> %arg) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.avx512bf16.mask.cvtneps2bf16.128(<4 x float> %arg, <8 x i16> undef, <4 x i1> <i1 true, i1 true, i1 true, i1 true>)
  %2 = shufflevector <8 x i16> %1, <8 x i16> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  ret <4 x i16> %2
}

declare <8 x i16> @llvm.x86.avx512bf16.mask.cvtneps2bf16.128(<4 x float>, <8 x i16>, <4 x i1>)

; The bf16 dot product intrinsics combine the bf16 pairs into single i32 elements, so bitcast the inputs to match.
define weak_odr <16 x float>  @dpbf16psx16(<16 x float> %init, <32 x i16> %a, <32 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <32 x i16> %a to <16 x i32>
  %2 = bitcast <32 x i16> %b to <16 x i32>
  %3 = tail call <16 x float> @llvm.x86.avx512bf16.dpbf16ps.512(<16 x float> %init, <16 x i32> %1, <16 x i32> %2)
  ret <16 x float> %3
}
declare <16 x float> @llvm.x86.avx512bf16.dpbf16ps.512(<16 x float>, <16 x i32>, <16 x i32>)

define weak_odr <8 x float>  @dpbf16psx8(<8 x float> %init, <16 x i16> %a, <16 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <16 x i16> %a to <8 x i32>
  %2 = bitcast <16 x i16> %b to <8 x i32>
  %3 = tail call <8 x float> @llvm.x86.avx512bf16.dpbf16ps.256(<8 x float> %init, <8 x i32> %1, <8 x i32> %2)
  ret <8 x float> %3
}
declare <8 x float> @llvm.x86.avx512bf16.dpbf16ps.256(<8 x float>, <8 x i32>, <8 x i32>)

define weak_odr <4 x float>  @dpbf16psx4(<4 x float> %init, <8 x i16> %a, <8 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <8 x i16> %a to <4 x i32>
  %2 = bitcast <8 x i16> %b to <4 x i32>
  %3 = tail call <4 x float> @llvm.x86.avx512bf16.dpbf16ps.128(<4 x float> %init, <4 x i32> %1, <4 x i32> %2)
  ret <4 x float> %3
}
declare <4 x float> @llvm.x86.avx512bf16.dpbf16ps.128(<4 x float>, <4 x i32>, <4 x i32>)

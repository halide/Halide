
; Split a 32 element f32 vector into two 16 element vectors to use the cvtne2ps2bf16 intrinsic.
define weak_odr <32 x i16>  @vcvtne2ps2bf16x32(<32 x float> %arg) nounwind alwaysinline {
  %1 = shufflevector <32 x float> %arg, <32 x float> poison, <16 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15>
  %2 = shufflevector <32 x float> %arg, <32 x float> poison, <16 x i32> <i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31>
  %3 = tail call <32 x i16> @llvm.x86.avx512bf16.cvtne2ps2bf16.512(<16 x float> %2, <16 x float> %1)
  ret <32 x i16> %3
}

declare <32 x i16> @llvm.x86.avx512bf16.cvtne2ps2bf16.512(<16 x float>, <16 x float>)

; LLVM does not have an unmasked version of cvtneps2bf16.128, so provide a wrapper around the masked version.
define weak_odr <4 x i16>  @vcvtneps2bf16x4(<4 x float> %arg) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.avx512bf16.mask.cvtneps2bf16.128(<4 x float> %arg, <8 x i16> poison, <4 x i1> <i1 true, i1 true, i1 true, i1 true>)
  %2 = shufflevector <8 x i16> %1, <8 x i16> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
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

define weak_odr <16 x i32>  @dpbusdx16(<16 x i32> %init, <64 x i8> %a, <64 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <64 x i8> %a to <16 x i32>
  %2 = bitcast <64 x i8> %b to <16 x i32>
  %3 = tail call <16 x i32> @llvm.x86.avx512.vpdpbusd.512(<16 x i32> %init, <16 x i32> %1, <16 x i32> %2)
  ret <16 x i32> %3
}
declare <16 x i32> @llvm.x86.avx512.vpdpbusd.512(<16 x i32>, <16 x i32>, <16 x i32>)

define weak_odr <8 x i32>  @dpbusdx8(<8 x i32> %init, <32 x i8> %a, <32 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <32 x i8> %a to <8 x i32>
  %2 = bitcast <32 x i8> %b to <8 x i32>
  %3 = tail call <8 x i32> @llvm.x86.avx512.vpdpbusd.256(<8 x i32> %init, <8 x i32> %1, <8 x i32> %2)
  ret <8 x i32> %3
}
declare <8 x i32> @llvm.x86.avx512.vpdpbusd.256(<8 x i32>, <8 x i32>, <8 x i32>)

define weak_odr <4 x i32>  @dpbusdx4(<4 x i32> %init, <16 x i8> %a, <16 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <16 x i8> %a to <4 x i32>
  %2 = bitcast <16 x i8> %b to <4 x i32>
  %3 = tail call <4 x i32> @llvm.x86.avx512.vpdpbusd.128(<4 x i32> %init, <4 x i32> %1, <4 x i32> %2)
  ret <4 x i32> %3
}
declare <4 x i32> @llvm.x86.avx512.vpdpbusd.128(<4 x i32>, <4 x i32>, <4 x i32>)

define weak_odr <16 x i32>  @dpwssdx16(<16 x i32> %init, <32 x i16> %a, <32 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <32 x i16> %a to <16 x i32>
  %2 = bitcast <32 x i16> %b to <16 x i32>
  %3 = tail call <16 x i32> @llvm.x86.avx512.vpdpwssd.512(<16 x i32> %init, <16 x i32> %1, <16 x i32> %2)
  ret <16 x i32> %3
}
declare <16 x i32> @llvm.x86.avx512.vpdpwssd.512(<16 x i32>, <16 x i32>, <16 x i32>)

define weak_odr <8 x i32>  @dpwssdx8(<8 x i32> %init, <16 x i16> %a, <16 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <16 x i16> %a to <8 x i32>
  %2 = bitcast <16 x i16> %b to <8 x i32>
  %3 = tail call <8 x i32> @llvm.x86.avx512.vpdpwssd.256(<8 x i32> %init, <8 x i32> %1, <8 x i32> %2)
  ret <8 x i32> %3
}
declare <8 x i32> @llvm.x86.avx512.vpdpwssd.256(<8 x i32>, <8 x i32>, <8 x i32>)

define weak_odr <4 x i32>  @dpwssdx4(<4 x i32> %init, <8 x i16> %a, <8 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <8 x i16> %a to <4 x i32>
  %2 = bitcast <8 x i16> %b to <4 x i32>
  %3 = tail call <4 x i32> @llvm.x86.avx512.vpdpwssd.128(<4 x i32> %init, <4 x i32> %1, <4 x i32> %2)
  ret <4 x i32> %3
}
declare <4 x i32> @llvm.x86.avx512.vpdpwssd.128(<4 x i32>, <4 x i32>, <4 x i32>)

define weak_odr <16 x i32>  @dpbusdsx16(<16 x i32> %init, <64 x i8> %a, <64 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <64 x i8> %a to <16 x i32>
  %2 = bitcast <64 x i8> %b to <16 x i32>
  %3 = tail call <16 x i32> @llvm.x86.avx512.vpdpbusds.512(<16 x i32> %init, <16 x i32> %1, <16 x i32> %2)
  ret <16 x i32> %3
}
declare <16 x i32> @llvm.x86.avx512.vpdpbusds.512(<16 x i32>, <16 x i32>, <16 x i32>)

define weak_odr <8 x i32>  @dpbusdsx8(<8 x i32> %init, <32 x i8> %a, <32 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <32 x i8> %a to <8 x i32>
  %2 = bitcast <32 x i8> %b to <8 x i32>
  %3 = tail call <8 x i32> @llvm.x86.avx512.vpdpbusds.256(<8 x i32> %init, <8 x i32> %1, <8 x i32> %2)
  ret <8 x i32> %3
}
declare <8 x i32> @llvm.x86.avx512.vpdpbusds.256(<8 x i32>, <8 x i32>, <8 x i32>)

define weak_odr <4 x i32>  @dpbusdsx4(<4 x i32> %init, <16 x i8> %a, <16 x i8> %b) nounwind alwaysinline {
  %1 = bitcast <16 x i8> %a to <4 x i32>
  %2 = bitcast <16 x i8> %b to <4 x i32>
  %3 = tail call <4 x i32> @llvm.x86.avx512.vpdpbusds.128(<4 x i32> %init, <4 x i32> %1, <4 x i32> %2)
  ret <4 x i32> %3
}
declare <4 x i32> @llvm.x86.avx512.vpdpbusds.128(<4 x i32>, <4 x i32>, <4 x i32>)

define weak_odr <16 x i32>  @dpwssdsx16(<16 x i32> %init, <32 x i16> %a, <32 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <32 x i16> %a to <16 x i32>
  %2 = bitcast <32 x i16> %b to <16 x i32>
  %3 = tail call <16 x i32> @llvm.x86.avx512.vpdpwssds.512(<16 x i32> %init, <16 x i32> %1, <16 x i32> %2)
  ret <16 x i32> %3
}
declare <16 x i32> @llvm.x86.avx512.vpdpwssds.512(<16 x i32>, <16 x i32>, <16 x i32>)

define weak_odr <8 x i32>  @dpwssdsx8(<8 x i32> %init, <16 x i16> %a, <16 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <16 x i16> %a to <8 x i32>
  %2 = bitcast <16 x i16> %b to <8 x i32>
  %3 = tail call <8 x i32> @llvm.x86.avx512.vpdpwssds.256(<8 x i32> %init, <8 x i32> %1, <8 x i32> %2)
  ret <8 x i32> %3
}
declare <8 x i32> @llvm.x86.avx512.vpdpwssds.256(<8 x i32>, <8 x i32>, <8 x i32>)

define weak_odr <4 x i32>  @dpwssdsx4(<4 x i32> %init, <8 x i16> %a, <8 x i16> %b) nounwind alwaysinline {
  %1 = bitcast <8 x i16> %a to <4 x i32>
  %2 = bitcast <8 x i16> %b to <4 x i32>
  %3 = tail call <4 x i32> @llvm.x86.avx512.vpdpwssds.128(<4 x i32> %init, <4 x i32> %1, <4 x i32> %2)
  ret <4 x i32> %3
}
declare <4 x i32> @llvm.x86.avx512.vpdpwssds.128(<4 x i32>, <4 x i32>, <4 x i32>)

define weak_odr <64 x i8> @abs_i8x64(<64 x i8> %arg) {
 %1 = tail call <64 x i8> @llvm.abs.v64i8(<64 x i8> %arg, i1 false)
 ret <64 x i8> %1
}
declare <64 x i8> @llvm.abs.v64i8(<64 x i8>, i1) nounwind readnone

define weak_odr <32 x i16> @abs_i16x32(<32 x i16> %arg) {
 %1 = tail call <32 x i16> @llvm.abs.v32i16(<32 x i16> %arg, i1 false)
 ret <32 x i16> %1
}
declare <32 x i16> @llvm.abs.v32i16(<32 x i16>, i1) nounwind readnone

define weak_odr <16 x i32> @abs_i32x16(<16 x i32> %arg) {
 %1 = tail call <16 x i32> @llvm.abs.v16i32(<16 x i32> %arg, i1 false)
 ret <16 x i32> %1
}
declare <16 x i32> @llvm.abs.v16i32(<16 x i32>, i1) nounwind readnone

define weak_odr <32 x i16> @hadd_pmadd_u8_avx512(<64 x i8> %a) nounwind alwaysinline {
  %1 = tail call <32 x i16> @llvm.x86.avx512.pmaddubs.w.512(<64 x i8> %a, <64 x i8> <i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1>)
  ret <32 x i16> %1
}

define weak_odr <32 x i16> @hadd_pmadd_i8_avx512(<64 x i8> %a) nounwind alwaysinline {
  %1 = tail call <32 x i16> @llvm.x86.avx512.pmaddubs.w.512(<64 x i8> <i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1>, <64 x i8> %a)
  ret <32 x i16> %1
}
declare <32 x i16> @llvm.x86.avx512.pmaddubs.w.512(<64 x i8>, <64 x i8>) nounwind readnone

define weak_odr <16 x i32> @wmul_pmaddwd_avx512(<16 x i16> %a, <16 x i16> %b) nounwind alwaysinline {
  %1 = zext <16 x i16> %a to <16 x i32>
  %2 = zext <16 x i16> %b to <16 x i32>
  %3 = bitcast <16 x i32> %1 to <32 x i16>
  %4 = bitcast <16 x i32> %2 to <32 x i16>
  %res = call <16 x i32> @llvm.x86.avx512.pmaddw.d.512(<32 x i16> %3, <32 x i16> %4)
  ret <16 x i32> %res
}

define weak_odr <16 x i32> @hadd_pmadd_i16_avx512(<32 x i16> %a) nounwind alwaysinline {
  %res = call <16 x i32> @llvm.x86.avx512.pmaddw.d.512(<32 x i16> %a, <32 x i16> <i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1>)
  ret <16 x i32> %res
}
declare <16 x i32> @llvm.x86.avx512.pmaddw.d.512(<32 x i16>, <32 x i16>) nounwind readnone
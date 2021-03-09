
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

define weak_odr <1024 x i8> @tileloadd64_i8(i16 %rows, i16 %colbytes, i8* %ptr, i64 %off, i64 %stride) nounwind alwaysinline readonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = tail call x86_amx @llvm.x86.tileloadd64.internal(i16 %rows, i16 %colbytes, i8* %1, i64 %stride) nounwind readonly
  %3 = bitcast x86_amx %2 to <1024 x i8>
  ret <1024 x i8> %3
}
declare x86_amx @llvm.x86.tileloadd64.internal(i16, i16, i8*, i64)

define weak_odr <256 x i32> @tdpbssd(i16 %rows, i16 %colbytes, i16 %acc, <256 x i32> %out, <1024 x i8> %lhs, <1024 x i8> %rhs) nounwind alwaysinline readnone {
  %1 = bitcast <1024 x i8> %lhs to x86_amx
  %2 = bitcast <1024 x i8> %rhs to x86_amx
  %3 = bitcast <256 x i32> %out to x86_amx
  %4 = tail call x86_amx @llvm.x86.tdpbssd.internal(i16 %rows, i16 %colbytes, i16 %acc, x86_amx %3, x86_amx %1, x86_amx %2) nounwind readnone
  %5 = bitcast x86_amx %4 to <256 x i32>
  ret <256 x i32> %5
}
declare x86_amx @llvm.x86.tdpbssd.internal(i16, i16, i16, x86_amx, x86_amx, x86_amx)

define weak_odr <2 x i1> @tilestored64(i16 %rows, i16 %cols, i8* %ptr, i64 %off, i64 %stride, <256 x i32> %val) nounwind alwaysinline writeonly {
  %1 = getelementptr i8, i8* %ptr, i64 %off
  %2 = bitcast <256 x i32> %val to x86_amx
  tail call void @llvm.x86.tilestored64.internal(i16 %rows, i16 %cols, i8* %1, i64 %stride, x86_amx %2) nounwind writeonly
  ret <2 x i1> zeroinitializer
}
declare void @llvm.x86.tilestored64.internal(i16, i16, i8*, i64, x86_amx)

; NB: Even though this should be readnone, that will cause LLVM to try to
; generate a single zero tile, and copy it each time it is used. However the AMX
; registers cannot be copied, so this causes compilation failures:
;     LLVM ERROR: Cannot emit physreg copy instruction
;       renamable $tmm1 = COPY renamable $tmm0
define weak_odr <256 x i32> @tilezero_i32(i16 %rows, i16 %colbytes) nounwind alwaysinline {
  %1 = tail call x86_amx @llvm.x86.tilezero.internal(i16 %rows, i16 %colbytes) nounwind
  %2 = bitcast x86_amx %1 to <256 x i32>
  ret <256 x i32> %2
}
declare x86_amx @llvm.x86.tilezero.internal(i16, i16)

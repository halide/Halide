
define weak_odr <16 x i32> @pmaddwdx16(<16 x i16> %a, <16 x i16> %b, <16 x i16> %c, <16 x i16> %d) nounwind alwaysinline {
  %1 = shufflevector <16 x i16> %a, <16 x i16> %c, <32 x i32> <i32 0, i32 16, i32 1, i32 17, i32 2, i32 18, i32 3, i32 19, i32 4, i32 20, i32 5, i32 21, i32 6, i32 22, i32 7, i32 23, i32 8, i32 24, i32 9, i32 25, i32 10, i32 26, i32 11, i32 27, i32 12, i32 28, i32 13, i32 29, i32 14, i32 30, i32 15, i32 31>
  %2 = shufflevector <16 x i16> %b, <16 x i16> %d, <32 x i32> <i32 0, i32 16, i32 1, i32 17, i32 2, i32 18, i32 3, i32 19, i32 4, i32 20, i32 5, i32 21, i32 6, i32 22, i32 7, i32 23, i32 8, i32 24, i32 9, i32 25, i32 10, i32 26, i32 11, i32 27, i32 12, i32 28, i32 13, i32 29, i32 14, i32 30, i32 15, i32 31>
  %3 = tail call <16 x i32> @llvm.x86.avx512.pmaddw.d.512(<32 x i16> %1, <32 x i16> %2)
  ret <16 x i32> %3
}

declare <16 x i32> @llvm.x86.avx512.pmaddw.d.512(<32 x i16>, <32 x i16>)

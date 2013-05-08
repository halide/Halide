
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

define weak_odr <16 x i8> @abs_i8x16(<16 x i8> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <16 x i8> @llvm.x86.ssse3.pabs.b.128(<16 x i8> %x)
  ret <16 x i8> %1
}

declare <16 x i8> @llvm.x86.ssse3.pabs.b.128(<16 x i8>) nounwind readnone

define weak_odr <8 x i16> @abs_i16x8(<8 x i16> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.ssse3.pabs.w.128(<8 x i16> %x)
  ret <8 x i16> %1
}

declare <8 x i16> @llvm.x86.ssse3.pabs.w.128(<8 x i16>) nounwind readnone

define weak_odr <4 x i32> @abs_i32x4(<4 x i32> %x) nounwind uwtable readnone alwaysinline {
  %1 = tail call <4 x i32> @llvm.x86.ssse3.pabs.d.128(<4 x i32> %x)
  ret <4 x i32> %1
}

declare <4 x i32> @llvm.x86.ssse3.pabs.d.128(<4 x i32>) nounwind readnone

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
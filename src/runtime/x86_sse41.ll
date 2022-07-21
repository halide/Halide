declare <8 x i16> @llvm.x86.sse41.packusdw(<4 x i32>, <4 x i32>) nounwind readnone

define weak_odr <8 x i16>  @packusdwx8(<8 x i32> %arg) nounwind alwaysinline {
  %1 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %2 = shufflevector <8 x i32> %arg, <8 x i32> undef, <4 x i32> < i32 4, i32 5, i32 6, i32 7>
  %3 = tail call <8 x i16> @llvm.x86.sse41.packusdw(<4 x i32> %1, <4 x i32> %2)
  ret <8 x i16> %3
}

define weak_odr <4 x float> @floor_f32x4(<4 x float> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse41.round.ps(<4 x float> %x, i32 1)
  ret <4 x float> %1
}

declare <4 x float> @llvm.x86.sse41.round.ps(<4 x float>, i32) nounwind readnone

define weak_odr <2 x double> @floor_f64x2(<2 x double> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse41.round.pd(<2 x double> %x, i32 1)
  ret <2 x double> %1
}

declare <2 x double> @llvm.x86.sse41.round.pd(<2 x double>, i32) nounwind readnone

define weak_odr <4 x float> @ceil_f32x4(<4 x float> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse41.round.ps(<4 x float> %x, i32 2)
  ret <4 x float> %1
}

define weak_odr <2 x double> @ceil_f64x2(<2 x double> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse41.round.pd(<2 x double> %x, i32 2)
  ret <2 x double> %1
}

define weak_odr <4 x float> @round_f32x4(<4 x float> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse41.round.ps(<4 x float> %x, i32 0)
  ret <4 x float> %1
}

define weak_odr <2 x double> @round_f64x2(<2 x double> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse41.round.pd(<2 x double> %x, i32 0)
  ret <2 x double> %1
}

define weak_odr <4 x float> @trunc_f32x4(<4 x float> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <4 x float> @llvm.x86.sse41.round.ps(<4 x float> %x, i32 3)
  ret <4 x float> %1
}

define weak_odr <2 x double> @trunc_f64x2(<2 x double> %x) nounwind uwtable readnone optsize inlinehint alwaysinline {
  %1 = tail call <2 x double> @llvm.x86.sse41.round.pd(<2 x double> %x, i32 3)
  ret <2 x double> %1
}

define weak_odr <16 x i8> @abs_i8x16(<16 x i8> %x) nounwind uwtable readnone alwaysinline {
  %1 = sub <16 x i8> zeroinitializer, %x
  %2 = icmp sgt <16 x i8> %x, zeroinitializer
  %3 = select <16 x i1> %2, <16 x i8> %x, <16 x i8> %1
  ret <16 x i8> %3
}

define weak_odr <8 x i16> @abs_i16x8(<8 x i16> %x) nounwind uwtable readnone alwaysinline {
  %1 = sub <8 x i16> zeroinitializer, %x
  %2 = icmp sgt <8 x i16> %x, zeroinitializer
  %3 = select <8 x i1> %2, <8 x i16> %x, <8 x i16> %1
  ret <8 x i16> %3
}

define weak_odr <4 x i32> @abs_i32x4(<4 x i32> %x) nounwind uwtable readnone alwaysinline {
  %1 = sub <4 x i32> zeroinitializer, %x
  %2 = icmp sgt <4 x i32> %x, zeroinitializer
  %3 = select <4 x i1> %2, <4 x i32> %x, <4 x i32> %1
  ret <4 x i32> %3
}

define weak_odr <8 x i16> @saturating_pmulhrswx8(<8 x i16> %a, <8 x i16> %b) nounwind uwtable readnone alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.ssse3.pmul.hr.sw.128(<8 x i16> %a, <8 x i16> %b)
  %2 = icmp eq <8 x i16> %a, <i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768>
  %3 = icmp eq <8 x i16> %b, <i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768, i16 -32768>
  %4 = and <8 x i1> %2, %3
  %5 = select <8 x i1> %4, <8 x i16> <i16 32767, i16 32767, i16 32767, i16 32767, i16 32767, i16 32767, i16 32767, i16 32767>, <8 x i16> %1
  ret <8 x i16> %5
}
declare <8 x i16> @llvm.x86.ssse3.pmul.hr.sw.128(<8 x i16>, <8 x i16>) nounwind readnone

define weak_odr <8 x i16> @hadd_pmadd_u8_sse3(<16 x i8> %a) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.ssse3.pmadd.ub.sw.128(<16 x i8> %a, <16 x i8> <i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1>)
  ret <8 x i16> %1
}

define weak_odr <8 x i16> @hadd_pmadd_i8_sse3(<16 x i8> %a) nounwind alwaysinline {
  %1 = tail call <8 x i16> @llvm.x86.ssse3.pmadd.ub.sw.128(<16 x i8> <i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1>, <16 x i8> %a)
  ret <8 x i16> %1
}
declare <8 x i16> @llvm.x86.ssse3.pmadd.ub.sw.128(<16 x i8>, <16 x i8>) nounwind readnone

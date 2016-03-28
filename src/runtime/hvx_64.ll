declare void @llvm.trap() noreturn nounwind

declare <16 x i32> @llvm.hexagon.V6.lo(<32 x i32>)
declare <16 x i32> @llvm.hexagon.V6.hi(<32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32>, <16 x i32>, i32)

define weak_odr <32 x i32> @halide.hexagon.interleave.w(<32 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg)
  %r = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %e, <16 x i32> %o, i32 -4)
  ret <32 x i32> %r
}

define weak_odr <64 x i16> @halide.hexagon.interleave.h(<64 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <64 x i16> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %e, <16 x i32> %o, i32 -2)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.interleave.b(<128 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i8> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %e, <16 x i32> %o, i32 -1)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}


declare <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vpackeh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vpackeb(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vpackoh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vpackob(<16 x i32>, <16 x i32>)

define weak_odr <32 x i32> @halide.hexagon.deinterleave.w(<32 x i32> %arg) nounwind uwtable readnone alwaysinline {
  ; TODO: Maybe there's an instruction I missed for this?
  %r = shufflevector <32 x i32> %arg, <32 x i32> undef, <32 x i32> <i32 0, i32 2, i32 4, i32 6, i32 8, i32 10, i32 12, i32 14, i32 16, i32 18, i32 20, i32 22, i32 24, i32 26, i32 28, i32 30, i32 1, i32 3, i32 5, i32 7, i32 9, i32 11, i32 13, i32 15, i32 17, i32 19, i32 21, i32 23, i32 25, i32 27, i32 29, i32 31>
  ret <32 x i32> %r
}

define weak_odr <64 x i16> @halide.hexagon.deinterleave.h(<64 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <64 x i16> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %re = call <16 x i32> @llvm.hexagon.V6.vpackeh(<16 x i32> %e, <16 x i32> %o)
  %ro = call <16 x i32> @llvm.hexagon.V6.vpackoh(<16 x i32> %e, <16 x i32> %o)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %ro, <16 x i32> %re)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.deinterleave.b(<128 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i8> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %re = call <16 x i32> @llvm.hexagon.V6.vpackeb(<16 x i32> %e, <16 x i32> %o)
  %ro = call <16 x i32> @llvm.hexagon.V6.vpackob(<16 x i32> %e, <16 x i32> %o)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %ro, <16 x i32> %re)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

declare void @llvm.trap() noreturn nounwind

declare <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32>)
declare <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32>, <32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vasrwhsat.128B(<32 x i32>, <32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vsathub.128B(<32 x i32>, <32 x i32>)

define weak_odr <64 x i32> @halide.hexagon.interleave.vw(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %r = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -4)
  ret <64 x i32> %r
}

define weak_odr <128 x i16> @halide.hexagon.interleave.vh(<128 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i16> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -2)
  %r = bitcast <64 x i32> %r_32 to <128 x i16>
  ret <128 x i16> %r
}

define weak_odr <256 x i8> @halide.hexagon.interleave.vb(<256 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <256 x i8> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -1)
  %r = bitcast <64 x i32> %r_32 to <256 x i8>
  ret <256 x i8> %r
}


declare <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackeh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackeb.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackoh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackob.128B(<32 x i32>, <32 x i32>)

define weak_odr <64 x i32> @halide.hexagon.deinterleave.vw(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  ; TODO: Maybe there's an instruction I missed for this?
  %r = shufflevector <64 x i32> %arg, <64 x i32> undef, <64 x i32> <i32 0, i32 2, i32 4, i32 6, i32 8, i32 10, i32 12, i32 14, i32 16, i32 18, i32 20, i32 22, i32 24, i32 26, i32 28, i32 30, i32 32, i32 34, i32 36, i32 38, i32 40, i32 42, i32 44, i32 46, i32 48, i32 50, i32 52, i32 54, i32 56, i32 58, i32 60, i32 62, i32 1, i32 3, i32 5, i32 7, i32 9, i32 11, i32 13, i32 15, i32 17, i32 19, i32 21, i32 23, i32 25, i32 27, i32 29, i32 31, i32 33, i32 35, i32 37, i32 39, i32 41, i32 43, i32 45, i32 47, i32 49, i32 51, i32 53, i32 55, i32 57, i32 59, i32 61, i32 63>
  ret <64 x i32> %r
}

define weak_odr <128 x i16> @halide.hexagon.deinterleave.vh(<128 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i16> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %re = call <32 x i32> @llvm.hexagon.V6.vpackeh.128B(<32 x i32> %o, <32 x i32> %e)
  %ro = call <32 x i32> @llvm.hexagon.V6.vpackoh.128B(<32 x i32> %o, <32 x i32> %e)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %ro, <32 x i32> %re)
  %r = bitcast <64 x i32> %r_32 to <128 x i16>
  ret <128 x i16> %r
}

define weak_odr <256 x i8> @halide.hexagon.deinterleave.vb(<256 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <256 x i8> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %re = call <32 x i32> @llvm.hexagon.V6.vpackeb.128B(<32 x i32> %o, <32 x i32> %e)
  %ro = call <32 x i32> @llvm.hexagon.V6.vpackob.128B(<32 x i32> %o, <32 x i32> %e)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %ro, <32 x i32> %re)
  %r = bitcast <64 x i32> %r_32 to <256 x i8>
  ret <256 x i8> %r
}

define weak_odr <128 x i32> @halide.hexagon.deinterleave.vw.dv(<128 x i32> %arg) nounwind uwtable readnone alwaysinline {
  ; First level of deinterleaving.
  %edv = shufflevector <128 x i32> %arg, <128 x i32> undef, <64 x i32> <i32 0, i32 2, i32 4, i32 6, i32 8, i32 10, i32 12, i32 14, i32 16, i32 18, i32 20, i32 22, i32 24, i32 26, i32 28, i32 30, i32 32, i32 34, i32 36, i32 38, i32 40, i32 42, i32 44, i32 46, i32 48, i32 50, i32 52, i32 54, i32 56, i32 58, i32 60, i32 62, i32 64, i32 66, i32 68, i32 70, i32 72, i32 74, i32 76, i32 78, i32 80, i32 82, i32 84, i32 86, i32 88, i32 90, i32 92, i32 94, i32 96, i32 98, i32 100, i32 102, i32 104, i32 106, i32 108, i32 110, i32 112, i32 114, i32 116, i32 118, i32 120, i32 122, i32 124, i32 126>
  %odv = shufflevector <128 x i32> %arg, <128 x i32> undef, <64 x i32> <i32 1, i32 3, i32 5, i32 7, i32 9, i32 11, i32 13, i32 15, i32 17, i32 19, i32 21, i32 23, i32 25, i32 27, i32 29, i32 31, i32 33, i32 35, i32 37, i32 39, i32 41, i32 43, i32 45, i32 47, i32 49, i32 51, i32 53, i32 55, i32 57, i32 59, i32 61, i32 63, i32 65, i32 67, i32 69, i32 71, i32 73, i32 75, i32 77, i32 79, i32 81, i32 83, i32 85, i32 87, i32 89, i32 91, i32 93, i32 95, i32 97, i32 99, i32 101, i32 103, i32 105, i32 107, i32 109, i32 111, i32 113, i32 115, i32 117, i32 119, i32 121, i32 123, i32 125, i32 127>
  ; Second level of deinterleaving.
  %edv_deint = call <64 x i32> @halide.hexagon.deinterleave.vw(<64 x i32> %edv)
  %odv_deint = call <64 x i32> @halide.hexagon.deinterleave.vw(<64 x i32> %odv)
  ; concatenate the two above and return the concatentated vector
  %r = shufflevector <64 x i32> %edv_deint, <64 x i32> %odv_deint, <128 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31, i32 32, i32 33, i32 34, i32 35, i32 36, i32 37, i32 38, i32 39, i32 40, i32 41, i32 42, i32 43, i32 44, i32 45, i32 46, i32 47, i32 48, i32 49, i32 50, i32 51, i32 52, i32 53, i32 54, i32 55, i32 56, i32 57, i32 58, i32 59, i32 60, i32 61, i32 62, i32 63, i32 64, i32 65, i32 66, i32 67, i32 68, i32 69, i32 70, i32 71, i32 72, i32 73, i32 74, i32 75, i32 76, i32 77, i32 78, i32 79, i32 80, i32 81, i32 82, i32 83, i32 84, i32 85, i32 86, i32 87, i32 88, i32 89, i32 90, i32 91, i32 92, i32 93, i32 94, i32 95, i32 96, i32 97, i32 98, i32 99, i32 100, i32 101, i32 102, i32 103, i32 104, i32 105, i32 106, i32 107, i32 108, i32 109, i32 110, i32 111, i32 112, i32 113, i32 114, i32 115, i32 116, i32 117, i32 118, i32 119, i32 120, i32 121, i32 122, i32 123, i32 124, i32 125, i32 126, i32 127>
  ret <128 x i32> %r
}

define weak_odr <128 x i8> @halide.hexagon.satub.shr.vw.w(<128 x i32> %arg0, i32 %arg1) nounwind uwtable readnone alwaysinline {
  %low_dv = shufflevector <128 x i32> %arg0, <128 x i32> undef, <64 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31, i32 32, i32 33, i32 34, i32 35, i32 36, i32 37, i32 38, i32 39, i32 40, i32 41, i32 42, i32 43, i32 44, i32 45, i32 46, i32 47, i32 48, i32 49, i32 50, i32 51, i32 52, i32 53, i32 54, i32 55, i32 56, i32 57, i32 58, i32 59, i32 60, i32 61, i32 62, i32 63>
  %high_dv = shufflevector <128 x i32> %arg0, <128 x i32> undef, <64 x i32> <i32 64, i32 65, i32 66, i32 67, i32 68, i32 69, i32 70, i32 71, i32 72, i32 73, i32 74, i32 75, i32 76, i32 77, i32 78, i32 79, i32 80, i32 81, i32 82, i32 83, i32 84, i32 85, i32 86, i32 87, i32 88, i32 89, i32 90, i32 91, i32 92, i32 93, i32 94, i32 95, i32 96, i32 97, i32 98, i32 99, i32 100, i32 101, i32 102, i32 103, i32 104, i32 105, i32 106, i32 107, i32 108, i32 109, i32 110, i32 111, i32 112, i32 113, i32 114, i32 115, i32 116, i32 117, i32 118, i32 119, i32 120, i32 121, i32 122, i32 123, i32 124, i32 125, i32 126, i32 127>
  %ldv_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %low_dv)
  %ldv_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %low_dv)
  %e = call <32 x i32> @llvm.hexagon.V6.vasrwhsat.128B(<32 x i32> %ldv_hi, <32 x i32> %ldv_lo, i32 %arg1)
  %hdv_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %high_dv)
  %hdv_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %high_dv)
  %o = call <32 x i32> @llvm.hexagon.V6.vasrwhsat.128B(<32 x i32> %hdv_hi, <32 x i32> %hdv_lo, i32 %arg1)
  %r = call <32 x i32> @llvm.hexagon.V6.vsathub.128B(<32 x i32> %o, <32 x i32> %e)
  %retval = bitcast <32 x i32> %r to <128 x i8>
  ret <128 x i8> %retval
}

declare <32 x i32> @llvm.hexagon.V6.lvsplatw.128B(i32)

define weak_odr i16 @halide.hexagon.dup2.b(i8 %arg) nounwind uwtable readnone alwaysinline {
  %arg_i16 = zext i8 %arg to i16
  %arg_i16_s = shl i16 %arg_i16, 8
  %r = or i16 %arg_i16, %arg_i16_s
  ret i16 %r
}

define weak_odr i32 @halide.hexagon.dup2.h(i16 %arg) nounwind uwtable readnone alwaysinline {
  %arg_i32 = zext i16 %arg to i32
  %arg_i32_s = shl i32 %arg_i32, 16
  %r = or i32 %arg_i32, %arg_i32_s
  ret i32 %r
}

define weak_odr i32 @halide.hexagon.dup4.b(i8 %arg) nounwind uwtable readnone alwaysinline {
  %halide.hexagon.dup2 = call i16 @halide.hexagon.dup2.b(i8 %arg)
  %halide.hexagon.dup4 = call i32 @halide.hexagon.dup2.h(i16 %halide.hexagon.dup2)
  ret i32 %halide.hexagon.dup4
}

define weak_odr <128 x i8> @halide.hexagon.splat.b(i8 %arg) nounwind uwtable readnone alwaysinline {
  %halide.hexagon.dup4 = call i32 @halide.hexagon.dup4.b(i8 %arg)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.lvsplatw.128B(i32 %halide.hexagon.dup4)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <64 x i16> @halide.hexagon.splat.h(i16 %arg) nounwind uwtable readnone alwaysinline {
  %halide.hexagon.dup2 = call i32 @halide.hexagon.dup2.h(i16 %arg)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.lvsplatw.128B(i32 %halide.hexagon.dup2)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

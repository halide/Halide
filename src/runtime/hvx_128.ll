declare void @llvm.trap() noreturn nounwind

declare <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32>)
declare <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32>, <32 x i32>, i32)
declare <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32>, <32 x i32>, i32)
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

define weak_odr <64 x i32> @halide.hexagon.deinterleave.vw(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %r = call <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -4)
  ret <64 x i32> %r
}

define weak_odr <128 x i16> @halide.hexagon.deinterleave.vh(<128 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i16> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %r_32 = call <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -2)
  %r = bitcast <64 x i32> %r_32 to <128 x i16>
  ret <128 x i16> %r
}

define weak_odr <256 x i8> @halide.hexagon.deinterleave.vb(<256 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <256 x i8> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg_32)
  %r_32 = call <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32> %o, <32 x i32> %e, i32 -1)
  %r = bitcast <64 x i32> %r_32 to <256 x i8>
  ret <256 x i8> %r
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
  %dup2 = call i16 @halide.hexagon.dup2.b(i8 %arg)
  %dup4 = call i32 @halide.hexagon.dup2.h(i16 %dup2)
  ret i32 %dup4
}

define weak_odr <128 x i8> @halide.hexagon.splat.b(i8 %arg) nounwind uwtable readnone alwaysinline {
  %dup4 = call i32 @halide.hexagon.dup4.b(i8 %arg)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.lvsplatw.128B(i32 %dup4)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <64 x i16> @halide.hexagon.splat.h(i16 %arg) nounwind uwtable readnone alwaysinline {
  %dup2 = call i32 @halide.hexagon.dup2.h(i16 %arg)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.lvsplatw.128B(i32 %dup2)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

; Implement various 32 bit multiplications.
declare <32 x i32> @llvm.hexagon.V6.vaslw.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vlsrw.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vmpyieoh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyiowh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyiewuh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vaddw.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshufeh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshufoh.128B(<32 x i32>, <32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32>, <32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vaddw.dv.128B(<64 x i32>, <64 x i32>)

define weak_odr <32 x i32> @halide.hexagon.mul.vw.vw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab_lo = call <32 x i32> @llvm.hexagon.V6.vmpyiewuh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab_hi = call <32 x i32> @llvm.hexagon.V6.vmpyieoh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab = call <32 x i32> @llvm.hexagon.V6.vaddw.128B(<32 x i32> %ab_hi, <32 x i32> %ab_lo)
  ret <32 x i32> %ab
}

define weak_odr <64 x i32> @halide.hexagon.mul.vw.vh(<64 x i32> %a, <64 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %a)
  %a_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %a)
  %b_hi = bitcast <64 x i16> %b to <32 x i32>
  %b_lo = call <32 x i32> @llvm.hexagon.V6.vaslw.128B(<32 x i32> %b_hi, i32 16)
  %ab_lo = call <32 x i32> @llvm.hexagon.V6.vmpyiowh.128B(<32 x i32> %a_lo, <32 x i32> %b_lo)
  %ab_hi = call <32 x i32> @llvm.hexagon.V6.vmpyiowh.128B(<32 x i32> %a_hi, <32 x i32> %b_hi)
  %ab = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %ab_hi, <32 x i32> %ab_lo)
  ret <64 x i32> %ab
}

define weak_odr <64 x i32> @halide.hexagon.mul.vw.vuh(<64 x i32> %a, <64 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %a)
  %a_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %a)
  %b_lo = bitcast <64 x i16> %b to <32 x i32>
  %b_hi = call <32 x i32> @llvm.hexagon.V6.vlsrw.128B(<32 x i32> %b_lo, i32 16)
  %ab_lo = call <32 x i32> @llvm.hexagon.V6.vmpyiewuh.128B(<32 x i32> %a_lo, <32 x i32> %b_lo)
  %ab_hi = call <32 x i32> @llvm.hexagon.V6.vmpyiewuh.128B(<32 x i32> %a_hi, <32 x i32> %b_hi)
  %ab = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %ab_hi, <32 x i32> %ab_lo)
  ret <64 x i32> %ab
}

define weak_odr <64 x i32> @halide.hexagon.mul.vuw.vuh(<64 x i32> %a, <64 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %a)
  %a_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %a)
  %a_e = call <32 x i32> @llvm.hexagon.V6.vshufeh.128B(<32 x i32> %a_hi, <32 x i32> %a_lo)
  %a_o = call <32 x i32> @llvm.hexagon.V6.vshufoh.128B(<32 x i32> %a_hi, <32 x i32> %a_lo)
  %b_32 = bitcast <64 x i16> %b to <32 x i32>
  %ab_e = call <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32> %a_e, <32 x i32> %b_32)
  %ab_o = call <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32> %a_o, <32 x i32> %b_32)
  %ab_o_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %ab_o)
  %ab_o_shifted_lo = call <32 x i32> @llvm.hexagon.V6.vaslw.128B(<32 x i32> %ab_o_lo, i32 16)
  %ab_o_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %ab_o)
  %ab_o_shifted_hi = call <32 x i32> @llvm.hexagon.V6.vaslw.128B(<32 x i32> %ab_o_hi, i32 16)
  %ab_o_shifted = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %ab_o_shifted_hi, <32 x i32> %ab_o_shifted_lo)
  %ab = call <64 x i32> @llvm.hexagon.V6.vaddw.dv.128B(<64 x i32> %ab_e, <64 x i32> %ab_o_shifted)
  ret <64 x i32> %ab
}

; Hexagon is missing shifts for byte sized operands.
declare <32 x i32> @llvm.hexagon.V6.vaslh.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vasrh.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vlsrh.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vaslhv.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vasrhv.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vlsrhv.128B(<32 x i32>, <32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32>, <32 x i32>)

define weak_odr <128 x i8> @halide.hexagon.shl.vub.ub(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %bw = zext i8 %b to i32
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vaslh.128B(<32 x i32> %aw_lo, i32 %bw)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vaslh.128B(<32 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <128 x i8> @halide.hexagon.shl.vb.b(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  ; A shift left is the same whether it is signed or not.
  %u = tail call <128 x i8> @halide.hexagon.shl.vub.ub(<128 x i8> %a, i8 %b)
  ret <128 x i8> %u
}

define weak_odr <128 x i8> @halide.hexagon.shr.vub.ub(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %bw = zext i8 %b to i32
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vlsrh.128B(<32 x i32> %aw_lo, i32 %bw)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vlsrh.128B(<32 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <128 x i8> @halide.hexagon.shr.vb.b(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %bw = zext i8 %b to i32
  %aw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %a_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vasrh.128B(<32 x i32> %aw_lo, i32 %bw)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vasrh.128B(<32 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}


define weak_odr <128 x i8> @halide.hexagon.shl.vub.vub(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %b_32 = bitcast <128 x i8> %b to <32 x i32>
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %bw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %b_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %bw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %bw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vaslhv.128B(<32 x i32> %aw_lo, <32 x i32> %bw_lo)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %bw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %bw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vaslhv.128B(<32 x i32> %aw_hi, <32 x i32> %bw_hi)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <128 x i8> @halide.hexagon.shl.vb.vb(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  ; A shift left is the same whether it is signed or not.
  %u = tail call <128 x i8> @halide.hexagon.shl.vub.vub(<128 x i8> %a, <128 x i8> %b)
  ret <128 x i8> %u
}

define weak_odr <128 x i8> @halide.hexagon.shr.vub.vub(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %b_32 = bitcast <128 x i8> %b to <32 x i32>
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %bw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %b_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %bw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %bw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vlsrhv.128B(<32 x i32> %aw_lo, <32 x i32> %bw_lo)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %bw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %bw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vlsrhv.128B(<32 x i32> %aw_hi, <32 x i32> %bw_hi)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

define weak_odr <128 x i8> @halide.hexagon.shr.vb.vb(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %b_32 = bitcast <128 x i8> %b to <32 x i32>
  %aw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %a_32)
  %bw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %b_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %bw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %bw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vasrhv.128B(<32 x i32> %aw_lo, <32 x i32> %bw_lo)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %bw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %bw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vasrhv.128B(<32 x i32> %aw_hi, <32 x i32> %bw_hi)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}
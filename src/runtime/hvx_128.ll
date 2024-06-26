
declare void @llvm.trap() noreturn nounwind

declare <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32>)
declare <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32>, <32 x i32>, i32)
declare <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32>, <32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vsatuwuh.128B(<32 x i32>, <32 x i32>)

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
declare i32 @llvm.hexagon.S2.vsplatrb(i32)

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
  %arg_i32 = zext i8 %arg to i32
  %dup4 = tail call i32 @llvm.hexagon.S2.vsplatrb(i32 %arg_i32)
  ret i32 %dup4
}

define weak_odr i32 @halide.hexagon.interleave.b.dup2.h(i8 %low, i8 %high) nounwind uwtable readnone alwaysinline {
  %high_i16 = zext i8 %high to i16
  %high_i16_s = shl i16 %high_i16, 8
  %low_i16 = zext i8 %low to i16
  %i16_const = or i16 %high_i16_s, %low_i16
  %r = call i32 @halide.hexagon.dup2.h(i16 %i16_const)
  ret i32 %r
}

; Implement various 32 bit multiplications.
declare <32 x i32> @llvm.hexagon.V6.vaslw.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vaslw.acc.128B(<32 x i32>, <32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vlsrw.128B(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vmpyieoh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyiowh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyiewuh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyiewuh.acc.128B(<32 x i32>, <32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshufeh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshufoh.128B(<32 x i32>, <32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32>, <32 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vmpyuhv.acc.128B(<64 x i32>, <32 x i32>, <32 x i32>)

define weak_odr <32 x i32> @halide.hexagon.mul.vw.vw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <32 x i32> @llvm.hexagon.V6.vmpyieoh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab = call <32 x i32> @llvm.hexagon.V6.vmpyiewuh.acc.128B(<32 x i32> %ab1, <32 x i32> %a, <32 x i32> %b)
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

; Do vaslw.acc on double vectors.
define private <64 x i32> @vaslw.acc.dv.128B(<64 x i32> %a, <64 x i32> %l, i32 %r) nounwind uwtable readnone alwaysinline {
  %a_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %a)
  %l_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %l)
  %s_lo = call <32 x i32> @llvm.hexagon.V6.vaslw.acc.128B(<32 x i32> %a_lo, <32 x i32> %l_lo, i32 %r)
  %a_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %a)
  %l_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %l)
  %s_hi = call <32 x i32> @llvm.hexagon.V6.vaslw.acc.128B(<32 x i32> %a_hi, <32 x i32> %l_hi, i32 %r)
  %s = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %s_hi, <32 x i32> %s_lo)
  ret <64 x i32> %s
}

define weak_odr <64 x i32> @halide.hexagon.mul.vuw.vuw(<64 x i32> %a, <64 x i32> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %a)
  %a_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %a)
  %b_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %b)
  %b_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %b)
  %a_e = call <32 x i32> @llvm.hexagon.V6.vshufeh.128B(<32 x i32> %a_hi, <32 x i32> %a_lo)
  %a_o = call <32 x i32> @llvm.hexagon.V6.vshufoh.128B(<32 x i32> %a_hi, <32 x i32> %a_lo)
  %b_e = call <32 x i32> @llvm.hexagon.V6.vshufeh.128B(<32 x i32> %b_hi, <32 x i32> %b_lo)
  %b_o = call <32 x i32> @llvm.hexagon.V6.vshufoh.128B(<32 x i32> %b_hi, <32 x i32> %b_lo)
  %ab_e = call <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32> %a_e, <32 x i32> %b_e)
  %ab_o1 = call <64 x i32> @llvm.hexagon.V6.vmpyuhv.128B(<32 x i32> %a_o, <32 x i32> %b_e)
  %ab_o = call <64 x i32> @llvm.hexagon.V6.vmpyuhv.acc.128B(<64 x i32> %ab_o1, <32 x i32> %a_e, <32 x i32> %b_o)
  %ab = call <64 x i32> @vaslw.acc.dv.128B(<64 x i32> %ab_e, <64 x i32> %ab_o, i32 16)
  ret <64 x i32> %ab
}

; 32 bit multiply keep high half.
declare <32 x i32> @llvm.hexagon.V6.vmpyewuh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyowh.sacc.128B(<32 x i32>, <32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyowh.rnd.sacc.128B(<32 x i32>, <32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vasrw.128B(<32 x i32>, i32)

define weak_odr <32 x i32> @halide.hexagon.trunc_mpy.vw.vw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <32 x i32> @llvm.hexagon.V6.vmpyewuh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab2 = call <32 x i32> @llvm.hexagon.V6.vmpyowh.sacc.128B(<32 x i32> %ab1, <32 x i32> %a, <32 x i32> %b)
  %ab = call <32 x i32> @llvm.hexagon.V6.vasrw.128B(<32 x i32> %ab2, i32 1)
  ret <32 x i32> %ab
}

define weak_odr <32 x i32> @halide.hexagon.trunc_satdw_mpy2.vw.vw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <32 x i32> @llvm.hexagon.V6.vmpyewuh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab = call <32 x i32> @llvm.hexagon.V6.vmpyowh.sacc.128B(<32 x i32> %ab1, <32 x i32> %a, <32 x i32> %b)
  ret <32 x i32> %ab
}

define weak_odr <32 x i32> @halide.hexagon.trunc_satdw_mpy2_rnd.vw.vw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <32 x i32> @llvm.hexagon.V6.vmpyewuh.128B(<32 x i32> %a, <32 x i32> %b)
  %ab = call <32 x i32> @llvm.hexagon.V6.vmpyowh.rnd.sacc.128B(<32 x i32> %ab1, <32 x i32> %a, <32 x i32> %b)
  ret <32 x i32> %ab
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

define weak_odr <128 x i8> @halide.hexagon.shl.vub.b(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %bw = sext i8 %b to i32
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
  %u = tail call <128 x i8> @halide.hexagon.shl.vub.b(<128 x i8> %a, i8 %b)
  ret <128 x i8> %u
}

define weak_odr <128 x i8> @halide.hexagon.shr.vub.b(<128 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %bw = sext i8 %b to i32
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
  %bw = sext i8 %b to i32
  %aw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %a_32)
  %aw_lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %aw)
  %sw_lo = call <32 x i32> @llvm.hexagon.V6.vasrh.128B(<32 x i32> %aw_lo, i32 %bw)
  %aw_hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %aw)
  %sw_hi = call <32 x i32> @llvm.hexagon.V6.vasrh.128B(<32 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb.128B(<32 x i32> %sw_hi, <32 x i32> %sw_lo)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}



define weak_odr <128 x i8> @halide.hexagon.shl.vub.vb(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %b_32 = bitcast <128 x i8> %b to <32 x i32>
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %bw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %b_32)
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
  %u = tail call <128 x i8> @halide.hexagon.shl.vub.vb(<128 x i8> %a, <128 x i8> %b)
  ret <128 x i8> %u
}

define weak_odr <128 x i8> @halide.hexagon.shr.vub.vb(<128 x i8> %a, <128 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <128 x i8> %a to <32 x i32>
  %b_32 = bitcast <128 x i8> %b to <32 x i32>
  %aw = call <64 x i32> @llvm.hexagon.V6.vzb.128B(<32 x i32> %a_32)
  %bw = call <64 x i32> @llvm.hexagon.V6.vsb.128B(<32 x i32> %b_32)
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

declare <64 x i32> @llvm.hexagon.V6.vmpabus.128B(<64 x i32>, i32)
declare <64 x i32> @llvm.hexagon.V6.vmpabus.acc.128B(<64 x i32>, <64 x i32>, i32)

define weak_odr <128 x i16> @halide.hexagon.add_2mpy.vub.vub.b.b(<128 x i8> %low_v, <128 x i8> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <128 x i8> %low_v to <32 x i32>
  %high = bitcast <128 x i8> %high_v to <32 x i32>
  %dv = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %high, <32 x i32> %low)
  %res = call <64 x i32> @llvm.hexagon.V6.vmpabus.128B(<64 x i32> %dv, i32 %const)
  %ret_val = bitcast <64 x i32> %res to <128 x i16>
  ret <128 x i16> %ret_val
}

define weak_odr <128 x i16> @halide.hexagon.acc_add_2mpy.vh.vub.vub.b.b(<128 x i16> %acc, <128 x i8> %low_v, <128 x i8> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %dv0 = bitcast <128 x i16> %acc to <64 x i32>
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <128 x i8> %low_v to <32 x i32>
  %high = bitcast <128 x i8> %high_v to <32 x i32>
  %dv1 = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %high, <32 x i32> %low)
  %res = call <64 x i32> @llvm.hexagon.V6.vmpabus.acc.128B(<64 x i32> %dv0, <64 x i32> %dv1, i32 %const)
  %ret_val = bitcast <64 x i32> %res to <128 x i16>
  ret <128 x i16> %ret_val
}

declare <64 x i32> @llvm.hexagon.V6.vmpahb.128B(<64 x i32>, i32)
declare <64 x i32> @llvm.hexagon.V6.vmpahb.acc.128B(<64 x i32>, <64 x i32>, i32)

define weak_odr <64 x i32> @halide.hexagon.add_2mpy.vh.vh.b.b(<64 x i16> %low_v, <64 x i16> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <64 x i16> %low_v to <32 x i32>
  %high = bitcast <64 x i16> %high_v to <32 x i32>
  %dv = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %high, <32 x i32> %low)
  %res = call <64 x i32> @llvm.hexagon.V6.vmpahb.128B(<64 x i32> %dv, i32 %const)
  ret <64 x i32> %res
}

define weak_odr <64 x i32> @halide.hexagon.acc_add_2mpy.vw.vh.vh.b.b(<64 x i32> %acc, <64 x i16> %low_v, <64 x i16> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <64 x i16> %low_v to <32 x i32>
  %high = bitcast <64 x i16> %high_v to <32 x i32>
  %dv1 = call <64 x i32> @llvm.hexagon.V6.vcombine.128B(<32 x i32> %high, <32 x i32> %low)
  %res = call <64 x i32> @llvm.hexagon.V6.vmpahb.acc.128B(<64 x i32> %acc, <64 x i32> %dv1, i32 %const)
  ret <64 x i32> %res
}

; Define a missing saturating narrow instruction in terms of a saturating narrowing shift.
declare <32 x i32> @llvm.hexagon.V6.vasrwuhsat.128B(<32 x i32>, <32 x i32>, i32)

define weak_odr <64 x i16> @halide.hexagon.trunc_satuh.vw(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vasrwuhsat.128B(<32 x i32> %o, <32 x i32> %e, i32 0)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

declare <32 x i32> @llvm.hexagon.V6.vpackeb.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vminuh.128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.lvsplath.128B(i32)

; We do not have saturating downcasts of unsigned 16bit types. So, we expand these
; in bitcode here.
; Note: pack_satub.vuh doesnt interleave its input.
define weak_odr <128 x i8> @halide.hexagon.pack_satub.vuh(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %max = call <32 x i32> @llvm.hexagon.V6.lvsplath.128B(i32 255)
  %lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %lo_sat = call <32 x i32> @llvm.hexagon.V6.vminuh.128B(<32 x i32> %lo, <32 x i32> %max)
  %hi_sat = call <32 x i32> @llvm.hexagon.V6.vminuh.128B(<32 x i32> %hi, <32 x i32> %max)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vpackeb.128B(<32 x i32> %hi_sat, <32 x i32> %lo_sat)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

; We cannot use the same strategy for halide.hexagon.pack_satuh.vuw as we did for halide.hexagon.pack_satub.vuh
; because HVX doesn't have a native min intrinsic for unsigned words like it does for unsigned half-words.
; Doing a signed min of an unsigned word with 65535 will make unsigned words > INT32_MAX become negative
; numbers does yielding the wrong result of 0 on subsequent saturation instead of 65535.
; Instead, we deinterleave the input double vector first and then use trunc_satuh.vuw. The latter is natively
; supported by the vsat instruction (vsatuwuh intrinsic). This is also the reason we don't have to
; provide halide.hexagon.trunc_satuh.vuw in the way that we had to provide halide.hexagon.trunc_satub.vuh below.
define weak_odr <64 x i16> @halide.hexagon.pack_satuh.vuw(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %lo = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %hi = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %deal_dv = call <64 x i32> @llvm.hexagon.V6.vdealvdd.128B(<32 x i32> %hi, <32 x i32> %lo, i32 -4)
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %deal_dv)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %deal_dv)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vsatuwuh.128B(<32 x i32> %o, <32 x i32> %e)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

declare <32 x i32> @llvm.hexagon.V6.vasruhubsat.128B(<32 x i32>, <32 x i32>, i32)
; This is the same as halide.hexagon.pack_satub.vuh except it interleaves its input.
define weak_odr <128 x i8> @halide.hexagon.trunc_satub.vuh(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %arg)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vasruhubsat.128B(<32 x i32> %o, <32 x i32> %e, i32 0)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

declare void @llvm.hexagon.V6.vgathermh.128B(i8*, i32, i32, <32 x i32>)
declare void @llvm.hexagon.V6.vgathermw.128B(i8*, i32, i32, <32 x i32>)

define weak_odr void @halide.hexagon.vgather.h.h(i8* %dst_base, i32 %dst_index, i8* %src_ptr, i32 %size, <64 x i16> %index) nounwind uwtable {
  %index32 = bitcast <64 x i16> %index to <32 x i32>
  %src = ptrtoint i8* %src_ptr to i32
  %dst_16base = bitcast i8* %dst_base to i16*
  %dst_16ptr = getelementptr i16, i16* %dst_16base, i32 %dst_index
  %dst_ptr = bitcast i16* %dst_16ptr to i8*
  call void @llvm.hexagon.V6.vgathermh.128B(i8* %dst_ptr, i32 %src, i32 %size, <32 x i32> %index32)
  ret void
}

define weak_odr void @halide.hexagon.vgather.w.w(i8* %dst_base, i32 %dst_index, i8* %src_ptr, i32 %size, <32 x i32> %index) nounwind uwtable {
  %src = ptrtoint i8* %src_ptr to i32
  %dst_32base = bitcast i8* %dst_base to i32*
  %dst_32ptr = getelementptr i32, i32* %dst_32base, i32 %dst_index
  %dst_ptr = bitcast i32* %dst_32ptr to i8*
  call void @llvm.hexagon.V6.vgathermw.128B(i8* %dst_ptr, i32 %src, i32 %size, <32 x i32> %index)
  ret void
}

declare void @llvm.hexagon.V6.vscattermh.128B(i32, i32, <32 x i32>, <32 x i32>)
declare void @llvm.hexagon.V6.vscattermw.128B(i32, i32, <32 x i32>, <32 x i32>)

define weak_odr void @halide.hexagon.vscatter.h.h(i8* %buf_ptr, i32 %size, <64 x i16> %idx, <64 x i16> %val) nounwind uwtable writeonly {
  %idx32 = bitcast <64 x i16> %idx to <32 x i32>
  %val32 = bitcast <64 x i16> %val to <32 x i32>
  %buf = ptrtoint i8* %buf_ptr to i32
  call void @llvm.hexagon.V6.vscattermh.128B(i32 %buf, i32 %size, <32 x i32> %idx32, <32 x i32> %val32) nounwind writeonly
  ret void
}

define weak_odr void @halide.hexagon.vscatter.w.w(i8* %buf_ptr, i32 %size, <32 x i32> %idx, <32 x i32> %val) nounwind uwtable writeonly {
  %buf = ptrtoint i8* %buf_ptr to i32
  call void @llvm.hexagon.V6.vscattermw.128B(i32 %buf, i32 %size, <32 x i32> %idx, <32 x i32> %val)
  ret void
}

declare void @llvm.hexagon.V6.vscattermh.add.128B(i32, i32, <32 x i32>, <32 x i32>)
declare void @llvm.hexagon.V6.vscattermw.add.128B(i32, i32, <32 x i32>, <32 x i32>)

define weak_odr void @halide.hexagon.vscatter_acc.h.h(i8* %buf_ptr, i32 %size, <64 x i16> %idx, <64 x i16> %val) nounwind uwtable writeonly {
  %idx32 = bitcast <64 x i16> %idx to <32 x i32>
  %val32 = bitcast <64 x i16> %val to <32 x i32>
  %buf = ptrtoint i8* %buf_ptr to i32
  call void @llvm.hexagon.V6.vscattermh.add.128B(i32 %buf, i32 %size, <32 x i32> %idx32, <32 x i32> %val32) nounwind writeonly
  ret void
}

define weak_odr void @halide.hexagon.vscatter_acc.w.w(i8* %buf_ptr, i32 %size, <32 x i32> %idx, <32 x i32> %val) nounwind uwtable writeonly {
  %buf = ptrtoint i8* %buf_ptr to i32
  call void @llvm.hexagon.V6.vscattermw.add.128B(i32 %buf, i32 %size, <32 x i32> %idx, <32 x i32> %val)
  ret void
}

define weak_odr void @halide.hexagon.scatter.release(i8* %ptr) nounwind uwtable {
  call void asm sideeffect "vmem($0 + #0):scatter_release\0A; v1 = vmem($0 + #0)\0A", "=*m,*m,~{v1}"(i8* elementtype(i8) %ptr, i8* elementtype(i8) %ptr)
  ret void
}

declare <64 x i32> @llvm.hexagon.V6.vrmpybusi.128B(<64 x i32>, i32, i32)
declare <64 x i32> @llvm.hexagon.V6.vrmpybusi.acc.128B(<64 x i32>, <64 x i32>, i32, i32)
declare <64 x i32> @llvm.hexagon.V6.vrmpyubi.128B(<64 x i32>, i32, i32)
declare <64 x i32> @llvm.hexagon.V6.vrmpyubi.acc.128B(<64 x i32>, <64 x i32>, i32, i32)

define weak_odr <128 x i32> @halide.hexagon.add_4mpy.stencil.helper(<64 x i32> %even_deinterleaved, <64 x i32> %odd_deinterleaved) nounwind uwtable readnone {
  %even = call <64 x i32> @halide.hexagon.interleave.vw(<64 x i32> %even_deinterleaved)
  %odd = call <64 x i32> @halide.hexagon.interleave.vw(<64 x i32> %odd_deinterleaved)
  %ee = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %even)
  %eo = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %even)
  %oe = call <32 x i32> @llvm.hexagon.V6.lo.128B(<64 x i32> %odd)
  %oo = call <32 x i32> @llvm.hexagon.V6.hi.128B(<64 x i32> %odd)
  %res_lo = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32> %oe, <32 x i32> %ee, i32 -4)
  %res_hi = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd.128B(<32 x i32> %oo, <32 x i32> %eo, i32 -4)
  %res = shufflevector <64 x i32> %res_lo, <64 x i32> %res_hi, <128 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31, i32 32, i32 33, i32 34, i32 35, i32 36, i32 37, i32 38, i32 39, i32 40, i32 41, i32 42, i32 43, i32 44, i32 45, i32 46, i32 47, i32 48, i32 49, i32 50, i32 51, i32 52, i32 53, i32 54, i32 55, i32 56, i32 57, i32 58, i32 59, i32 60, i32 61, i32 62, i32 63, i32 64, i32 65, i32 66, i32 67, i32 68, i32 69, i32 70, i32 71, i32 72, i32 73, i32 74, i32 75, i32 76, i32 77, i32 78, i32 79, i32 80, i32 81, i32 82, i32 83, i32 84, i32 85, i32 86, i32 87, i32 88, i32 89, i32 90, i32 91, i32 92, i32 93, i32 94, i32 95, i32 96, i32 97, i32 98, i32 99, i32 100, i32 101, i32 102, i32 103, i32 104, i32 105, i32 106, i32 107, i32 108, i32 109, i32 110, i32 111, i32 112, i32 113, i32 114, i32 115, i32 116, i32 117, i32 118, i32 119, i32 120, i32 121, i32 122, i32 123, i32 124, i32 125, i32 126, i32 127>
  ret <128 x i32> %res
}

define weak_odr <128 x i32> @halide.hexagon.add_4mpy.vub.b.stencil(<256 x i8> %dv, i32 %const) nounwind uwtable readnone {
  %dv32 = bitcast <256 x i8> %dv to <64 x i32>
  %even_deinterleaved = call <64 x i32> @llvm.hexagon.V6.vrmpybusi.128B(<64 x i32> %dv32, i32 %const, i32 0)
  %odd_deinterleaved = call <64 x i32> @llvm.hexagon.V6.vrmpybusi.128B(<64 x i32> %dv32, i32 %const, i32 1)
  %res = call <128 x i32> @halide.hexagon.add_4mpy.stencil.helper(<64 x i32> %even_deinterleaved, <64 x i32> %odd_deinterleaved)
  ret <128 x i32> %res
}

define weak_odr <128 x i32> @halide.hexagon.add_4mpy.vub.ub.stencil(<256 x i8> %dv, i32 %const) nounwind uwtable readnone {
  %dv32 = bitcast <256 x i8> %dv to <64 x i32>
  %even_deinterleaved = call <64 x i32> @llvm.hexagon.V6.vrmpyubi.128B(<64 x i32> %dv32, i32 %const, i32 0)
  %odd_deinterleaved = call <64 x i32> @llvm.hexagon.V6.vrmpyubi.128B(<64 x i32> %dv32, i32 %const, i32 1)
  %res = call <128 x i32> @halide.hexagon.add_4mpy.stencil.helper(<64 x i32> %even_deinterleaved, <64 x i32> %odd_deinterleaved)
  ret <128 x i32> %res
}

declare void @llvm.trap() noreturn nounwind

declare <16 x i32> @llvm.hexagon.V6.lo(<32 x i32>)
declare <16 x i32> @llvm.hexagon.V6.hi(<32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32>, <16 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vdealvdd(<16 x i32>, <16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vasrwhsat(<16 x i32>, <16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vsathub(<16 x i32>, <16 x i32>)

define weak_odr <32 x i32> @halide.hexagon.interleave.vw(<32 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg)
  %r = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %o, <16 x i32> %e, i32 -4)
  ret <32 x i32> %r
}

define weak_odr <64 x i16> @halide.hexagon.interleave.vh(<64 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <64 x i16> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %o, <16 x i32> %e, i32 -2)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.interleave.vb(<128 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i8> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffvdd(<16 x i32> %o, <16 x i32> %e, i32 -1)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}


declare <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32>, <16 x i32>)

define weak_odr <32 x i32> @halide.hexagon.deinterleave.vw(<32 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg)
  %r = call <32 x i32> @llvm.hexagon.V6.vdealvdd(<16 x i32> %o, <16 x i32> %e, i32 -4)
  ret <32 x i32> %r
}

define weak_odr <64 x i16> @halide.hexagon.deinterleave.vh(<64 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <64 x i16> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vdealvdd(<16 x i32> %o, <16 x i32> %e, i32 -2)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.deinterleave.vb(<128 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i8> %arg to <32 x i32>
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg_32)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg_32)
  %r_32 = call <32 x i32> @llvm.hexagon.V6.vdealvdd(<16 x i32> %o, <16 x i32> %e, i32 -1)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

declare <16 x i32> @llvm.hexagon.V6.lvsplatw(i32)

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

define weak_odr i32 @halide.hexagon.interleave.b.dup2.h(i8 %low, i8 %high) nounwind uwtable readnone alwaysinline {
  %high_i16 = zext i8 %high to i16
  %high_i16_s = shl i16 %high_i16, 8
  %low_i16 = zext i8 %low to i16
  %i16_const = or i16 %high_i16_s, %low_i16
  %r = call i32 @halide.hexagon.dup2.h(i16 %i16_const)
  ret i32 %r
}

define weak_odr <64 x i8> @halide.hexagon.splat.b(i8 %arg) nounwind uwtable readnone alwaysinline {
  %dup4 = call i32 @halide.hexagon.dup4.b(i8 %arg)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.lvsplatw(i32 %dup4)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

define weak_odr <32 x i16> @halide.hexagon.splat.h(i16 %arg) nounwind uwtable readnone alwaysinline {
  %dup2 = call i32 @halide.hexagon.dup2.h(i16 %arg)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.lvsplatw(i32 %dup2)
  %r = bitcast <16 x i32> %r_32 to <32 x i16>
  ret <32 x i16> %r
}

; Implement various 32 bit multiplications.
declare <16 x i32> @llvm.hexagon.V6.vaslw(<16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vaslw.acc(<16 x i32>, <16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vlsrw(<16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vmpyieoh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vmpyiowh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vmpyiewuh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vmpyiewuh.acc(<16 x i32>, <16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vshufeh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vshufoh(<16 x i32>, <16 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyuhv(<16 x i32>, <16 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vmpyuhv.acc(<32 x i32>, <16 x i32>, <16 x i32>)

define weak_odr <16 x i32> @halide.hexagon.mul.vw.vw(<16 x i32> %a, <16 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <16 x i32> @llvm.hexagon.V6.vmpyieoh(<16 x i32> %a, <16 x i32> %b)
  %ab = call <16 x i32> @llvm.hexagon.V6.vmpyiewuh.acc(<16 x i32> %ab1, <16 x i32> %a, <16 x i32> %b)
  ret <16 x i32> %ab
}

define weak_odr <32 x i32> @halide.hexagon.mul.vw.vh(<32 x i32> %a, <32 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %a)
  %a_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %a)
  %b_hi = bitcast <32 x i16> %b to <16 x i32>
  %b_lo = call <16 x i32> @llvm.hexagon.V6.vaslw(<16 x i32> %b_hi, i32 16)
  %ab_lo = call <16 x i32> @llvm.hexagon.V6.vmpyiowh(<16 x i32> %a_lo, <16 x i32> %b_lo)
  %ab_hi = call <16 x i32> @llvm.hexagon.V6.vmpyiowh(<16 x i32> %a_hi, <16 x i32> %b_hi)
  %ab = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %ab_hi, <16 x i32> %ab_lo)
  ret <32 x i32> %ab
}

define weak_odr <32 x i32> @halide.hexagon.mul.vw.vuh(<32 x i32> %a, <32 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %a)
  %a_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %a)
  %b_lo = bitcast <32 x i16> %b to <16 x i32>
  %b_hi = call <16 x i32> @llvm.hexagon.V6.vlsrw(<16 x i32> %b_lo, i32 16)
  %ab_lo = call <16 x i32> @llvm.hexagon.V6.vmpyiewuh(<16 x i32> %a_lo, <16 x i32> %b_lo)
  %ab_hi = call <16 x i32> @llvm.hexagon.V6.vmpyiewuh(<16 x i32> %a_hi, <16 x i32> %b_hi)
  %ab = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %ab_hi, <16 x i32> %ab_lo)
  ret <32 x i32> %ab
}

; Do vaslw.acc on double vectors.
define private <32 x i32> @vaslw.acc.dv(<32 x i32> %a, <32 x i32> %l, i32 %r) nounwind uwtable readnone alwaysinline {
  %a_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %a)
  %l_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %l)
  %s_lo = call <16 x i32> @llvm.hexagon.V6.vaslw.acc(<16 x i32> %a_lo, <16 x i32> %l_lo, i32 %r)
  %a_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %a)
  %l_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %l)
  %s_hi = call <16 x i32> @llvm.hexagon.V6.vaslw.acc(<16 x i32> %a_hi, <16 x i32> %l_hi, i32 %r)
  %s = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %s_hi, <16 x i32> %s_lo)
  ret <32 x i32> %s
}

define weak_odr <32 x i32> @halide.hexagon.mul.vuw.vuh(<32 x i32> %a, <32 x i16> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %a)
  %a_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %a)
  %a_e = call <16 x i32> @llvm.hexagon.V6.vshufeh(<16 x i32> %a_hi, <16 x i32> %a_lo)
  %a_o = call <16 x i32> @llvm.hexagon.V6.vshufoh(<16 x i32> %a_hi, <16 x i32> %a_lo)
  %b_32 = bitcast <32 x i16> %b to <16 x i32>
  %ab_e = call <32 x i32> @llvm.hexagon.V6.vmpyuhv(<16 x i32> %a_e, <16 x i32> %b_32)
  %ab_o = call <32 x i32> @llvm.hexagon.V6.vmpyuhv(<16 x i32> %a_o, <16 x i32> %b_32)
  %ab = call <32 x i32> @vaslw.acc.dv(<32 x i32> %ab_e, <32 x i32> %ab_o, i32 16)
  ret <32 x i32> %ab
}

define weak_odr <32 x i32> @halide.hexagon.mul.vuw.vuw(<32 x i32> %a, <32 x i32> %b) nounwind uwtable readnone alwaysinline {
  %a_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %a)
  %a_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %a)
  %b_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %b)
  %b_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %b)
  %a_e = call <16 x i32> @llvm.hexagon.V6.vshufeh(<16 x i32> %a_hi, <16 x i32> %a_lo)
  %a_o = call <16 x i32> @llvm.hexagon.V6.vshufoh(<16 x i32> %a_hi, <16 x i32> %a_lo)
  %b_e = call <16 x i32> @llvm.hexagon.V6.vshufeh(<16 x i32> %b_hi, <16 x i32> %b_lo)
  %b_o = call <16 x i32> @llvm.hexagon.V6.vshufoh(<16 x i32> %b_hi, <16 x i32> %b_lo)
  %ab_e = call <32 x i32> @llvm.hexagon.V6.vmpyuhv(<16 x i32> %a_e, <16 x i32> %b_e)
  %ab_o1 = call <32 x i32> @llvm.hexagon.V6.vmpyuhv(<16 x i32> %a_o, <16 x i32> %b_e)
  %ab_o = call <32 x i32> @llvm.hexagon.V6.vmpyuhv.acc(<32 x i32> %ab_o1, <16 x i32> %a_e, <16 x i32> %b_o)
  %ab = call <32 x i32> @vaslw.acc.dv(<32 x i32> %ab_e, <32 x i32> %ab_o, i32 16)
  ret <32 x i32> %ab
}

; 32 bit multiply keep high half.
declare <16 x i32> @llvm.hexagon.V6.vmpyewuh(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vmpyowh.sacc(<16 x i32>, <16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vmpyowh.rnd.sacc(<16 x i32>, <16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vasrw(<16 x i32>, i32)

define weak_odr <16 x i32> @halide.hexagon.trunc_mpy.vw.vw(<16 x i32> %a, <16 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <16 x i32> @llvm.hexagon.V6.vmpyewuh(<16 x i32> %a, <16 x i32> %b)
  %ab2 = call <16 x i32> @llvm.hexagon.V6.vmpyowh.sacc(<16 x i32> %ab1, <16 x i32> %a, <16 x i32> %b)
  %ab = call <16 x i32> @llvm.hexagon.V6.vasrw(<16 x i32> %ab2, i32 1)
  ret <16 x i32> %ab
}

define weak_odr <16 x i32> @halide.hexagon.trunc_satdw_mpy2.vw.vw(<16 x i32> %a, <16 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <16 x i32> @llvm.hexagon.V6.vmpyewuh(<16 x i32> %a, <16 x i32> %b)
  %ab = call <16 x i32> @llvm.hexagon.V6.vmpyowh.sacc(<16 x i32> %ab1, <16 x i32> %a, <16 x i32> %b)
  ret <16 x i32> %ab
}

define weak_odr <16 x i32> @halide.hexagon.trunc_satdw_mpy2_rnd.vw.vw(<16 x i32> %a, <16 x i32> %b) nounwind uwtable readnone alwaysinline {
  %ab1 = call <16 x i32> @llvm.hexagon.V6.vmpyewuh(<16 x i32> %a, <16 x i32> %b)
  %ab = call <16 x i32> @llvm.hexagon.V6.vmpyowh.rnd.sacc(<16 x i32> %ab1, <16 x i32> %a, <16 x i32> %b)
  ret <16 x i32> %ab
}

; Hexagon is missing shifts for byte sized operands.
declare <16 x i32> @llvm.hexagon.V6.vaslh(<16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vasrh(<16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vlsrh(<16 x i32>, i32)
declare <16 x i32> @llvm.hexagon.V6.vaslhv(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vasrhv(<16 x i32>, <16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vlsrhv(<16 x i32>, <16 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vsb(<16 x i32>)
declare <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32>, <16 x i32>)

define weak_odr <64 x i8> @halide.hexagon.shl.vub.ub(<64 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %bw = zext i8 %b to i32
  %aw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %a_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vaslh(<16 x i32> %aw_lo, i32 %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vaslh(<16 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

define weak_odr <64 x i8> @halide.hexagon.shl.vb.b(<64 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  ; A shift left is the same whether it is signed or not.
  %u = tail call <64 x i8> @halide.hexagon.shl.vub.ub(<64 x i8> %a, i8 %b)
  ret <64 x i8> %u
}

define weak_odr <64 x i8> @halide.hexagon.shr.vub.ub(<64 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %bw = zext i8 %b to i32
  %aw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %a_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vlsrh(<16 x i32> %aw_lo, i32 %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vlsrh(<16 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

define weak_odr <64 x i8> @halide.hexagon.shr.vb.b(<64 x i8> %a, i8 %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %bw = zext i8 %b to i32
  %aw = call <32 x i32> @llvm.hexagon.V6.vsb(<16 x i32> %a_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vasrh(<16 x i32> %aw_lo, i32 %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vasrh(<16 x i32> %aw_hi, i32 %bw)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}


define weak_odr <64 x i8> @halide.hexagon.shl.vub.vub(<64 x i8> %a, <64 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %b_32 = bitcast <64 x i8> %b to <16 x i32>
  %aw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %a_32)
  %bw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %b_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %bw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %bw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vaslhv(<16 x i32> %aw_lo, <16 x i32> %bw_lo)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %bw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vaslhv(<16 x i32> %aw_hi, <16 x i32> %bw_hi)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

define weak_odr <64 x i8> @halide.hexagon.shl.vb.vb(<64 x i8> %a, <64 x i8> %b) nounwind uwtable readnone alwaysinline {
  ; A shift left is the same whether it is signed or not.
  %u = tail call <64 x i8> @halide.hexagon.shl.vub.vub(<64 x i8> %a, <64 x i8> %b)
  ret <64 x i8> %u
}

define weak_odr <64 x i8> @halide.hexagon.shr.vub.vub(<64 x i8> %a, <64 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %b_32 = bitcast <64 x i8> %b to <16 x i32>
  %aw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %a_32)
  %bw = call <32 x i32> @llvm.hexagon.V6.vzb(<16 x i32> %b_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %bw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %bw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vlsrhv(<16 x i32> %aw_lo, <16 x i32> %bw_lo)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %bw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vlsrhv(<16 x i32> %aw_hi, <16 x i32> %bw_hi)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

define weak_odr <64 x i8> @halide.hexagon.shr.vb.vb(<64 x i8> %a, <64 x i8> %b) nounwind uwtable readnone alwaysinline {
  %a_32 = bitcast <64 x i8> %a to <16 x i32>
  %b_32 = bitcast <64 x i8> %b to <16 x i32>
  %aw = call <32 x i32> @llvm.hexagon.V6.vsb(<16 x i32> %a_32)
  %bw = call <32 x i32> @llvm.hexagon.V6.vsb(<16 x i32> %b_32)
  %aw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %aw)
  %bw_lo = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %bw)
  %sw_lo = call <16 x i32> @llvm.hexagon.V6.vasrhv(<16 x i32> %aw_lo, <16 x i32> %bw_lo)
  %aw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %aw)
  %bw_hi = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %bw)
  %sw_hi = call <16 x i32> @llvm.hexagon.V6.vasrhv(<16 x i32> %aw_hi, <16 x i32> %bw_hi)
  %r_32 = tail call <16 x i32> @llvm.hexagon.V6.vshuffeb(<16 x i32> %sw_hi, <16 x i32> %sw_lo)
  %r = bitcast <16 x i32> %r_32 to <64 x i8>
  ret <64 x i8> %r
}

declare <32 x i32> @llvm.hexagon.V6.vmpabus(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vmpabus.acc(<32 x i32>, <32 x i32>, i32)

define weak_odr <64 x i16> @halide.hexagon.add_2mpy.vub.vub.b.b(<64 x i8> %low_v, <64 x i8> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <64 x i8> %low_v to <16 x i32>
  %high = bitcast <64 x i8> %high_v to <16 x i32>
  %dv = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %high, <16 x i32> %low)
  %res = call <32 x i32> @llvm.hexagon.V6.vmpabus(<32 x i32> %dv, i32 %const)
  %ret_val = bitcast <32 x i32> %res to <64 x i16>
  ret <64 x i16> %ret_val
}

define weak_odr <64 x i16> @halide.hexagon.acc_add_2mpy.vh.vub.vub.b.b(<64 x i16> %acc, <64 x i8> %low_v, <64 x i8> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %dv0 = bitcast <64 x i16> %acc to <32 x i32>
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <64 x i8> %low_v to <16 x i32>
  %high = bitcast <64 x i8> %high_v to <16 x i32>
  %dv1 = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %high, <16 x i32> %low)
  %res = call <32 x i32> @llvm.hexagon.V6.vmpabus.acc(<32 x i32> %dv0, <32 x i32> %dv1, i32 %const)
  %ret_val = bitcast <32 x i32> %res to <64 x i16>
  ret <64 x i16> %ret_val
}

declare <32 x i32> @llvm.hexagon.V6.vmpahb(<32 x i32>, i32)
declare <32 x i32> @llvm.hexagon.V6.vmpahb.acc(<32 x i32>, <32 x i32>, i32)

define weak_odr <32 x i32> @halide.hexagon.add_2mpy.vh.vh.b.b(<32 x i16> %low_v, <32 x i16> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <32 x i16> %low_v to <16 x i32>
  %high = bitcast <32 x i16> %high_v to <16 x i32>
  %dv = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %high, <16 x i32> %low)
  %res = call <32 x i32> @llvm.hexagon.V6.vmpahb(<32 x i32> %dv, i32 %const)
  ret <32 x i32> %res
}

define weak_odr <32 x i32> @halide.hexagon.acc_add_2mpy.vw.vh.vh.b.b(<32 x i32> %acc, <32 x i16> %low_v, <32 x i16> %high_v, i8 %low_c, i8 %high_c) nounwind uwtable readnone {
  %const = call i32 @halide.hexagon.interleave.b.dup2.h(i8 %low_c, i8 %high_c)
  %low = bitcast <32 x i16> %low_v to <16 x i32>
  %high = bitcast <32 x i16> %high_v to <16 x i32>
  %dv1 = call <32 x i32> @llvm.hexagon.V6.vcombine(<16 x i32> %high, <16 x i32> %low)
  %res = call <32 x i32> @llvm.hexagon.V6.vmpahb.acc(<32 x i32> %acc, <32 x i32> %dv1, i32 %const)
  ret <32 x i32> %res
}

; Define a missing saturating narrow instruction in terms of a saturating narrowing shift.
declare <16 x i32> @llvm.hexagon.V6.vasrwuhsat(<16 x i32>, <16 x i32>, i32)

define weak_odr <32 x i16> @halide.hexagon.trunc_satuh.vw(<32 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <16 x i32> @llvm.hexagon.V6.lo(<32 x i32> %arg)
  %o = call <16 x i32> @llvm.hexagon.V6.hi(<32 x i32> %arg)
  %r_32 = call <16 x i32> @llvm.hexagon.V6.vasrwuhsat(<16 x i32> %o, <16 x i32> %e, i32 0)
  %r = bitcast <16 x i32> %r_32 to <32 x i16>
  ret <32 x i16> %r
}

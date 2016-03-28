declare <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32>)
declare <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32>)
declare <64 x i32> @llvm.hexagon.V6.vshuffvdd_128B(<32 x i32>, <32 x i32>, i32)

define weak_odr <64 x i32> @halide.hexagon.interleave.w(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg)
  %r = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd_128B(<32 x i32> %e, <32 x i32> %o, i32 -4)
  ret <64 x i32> %r
}

define weak_odr <128 x i16> @halide.hexagon.interleave.h(<128 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i16> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg_32)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd_128B(<32 x i32> %e, <32 x i32> %o, i32 -2)
  %r = bitcast <64 x i32> %r_32 to <128 x i16>
  ret <128 x i16> %r
}

define weak_odr <256 x i8> @halide.hexagon.interleave.b(<256 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <256 x i8> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg_32)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vshuffvdd_128B(<32 x i32> %e, <32 x i32> %o, i32 -1)
  %r = bitcast <64 x i32> %r_32 to <256 x i8>
  ret <256 x i8> %r
}


declare <64 x i32> @llvm.hexagon.V6.vcombine_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackew_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackeh_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackeb_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackow_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackoh_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vpackob_128B(<32 x i32>, <32 x i32>)

define weak_odr <64 x i32> @halide.hexagon.deinterleave.w(<64 x i32> %arg) nounwind uwtable readnone alwaysinline {
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg)
  %re = call <32 x i32> @llvm.hexagon.V6.vpackew_128B(<32 x i32> %e, <32 x i32> %o)
  %ro = call <32 x i32> @llvm.hexagon.V6.vpackow_128B(<32 x i32> %e, <32 x i32> %o)
  %r = tail call <64 x i32> @llvm.hexagon.V6.vcombine_128B(<32 x i32> %ro, <32 x i32> %re)
  ret <64 x i32> %r
}

define weak_odr <128 x i16> @halide.hexagon.deinterleave.h(<128 x i16> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <128 x i16> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg_32)
  %re = call <32 x i32> @llvm.hexagon.V6.vpackew_128B(<32 x i32> %e, <32 x i32> %o)
  %ro = call <32 x i32> @llvm.hexagon.V6.vpackow_128B(<32 x i32> %e, <32 x i32> %o)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vcombine_128B(<32 x i32> %ro, <32 x i32> %re)
  %r = bitcast <64 x i32> %r_32 to <128 x i16>
  ret <128 x i16> %r
}

define weak_odr <256 x i8> @halide.hexagon.deinterleave.b(<256 x i8> %arg) nounwind uwtable readnone alwaysinline {
  %arg_32 = bitcast <256 x i8> %arg to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %arg_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %arg_32)
  %re = call <32 x i32> @llvm.hexagon.V6.vpackew_128B(<32 x i32> %e, <32 x i32> %o)
  %ro = call <32 x i32> @llvm.hexagon.V6.vpackow_128B(<32 x i32> %e, <32 x i32> %o)
  %r_32 = tail call <64 x i32> @llvm.hexagon.V6.vcombine_128B(<32 x i32> %ro, <32 x i32> %re)
  %r = bitcast <64 x i32> %r_32 to <256 x i8>
  ret <256 x i8> %r
}


declare <32 x i32> @llvm.hexagon.V6.vshuffeb_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshuffeh_128B(<32 x i32>, <32 x i32>)

define weak_odr <64 x i16> @halide.hexagon.trunchi.h(<64 x i32> %oe) nounwind uwtable readnone alwaysinline {
  %oe_32 = bitcast <64 x i32> %oe to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %oe_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %oe_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeh_128B(<32 x i32> %o, <32 x i32> %e)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.trunchi.b(<128 x i16> %oe) nounwind uwtable readnone alwaysinline {
  %oe_32 = bitcast <128 x i16> %oe to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %oe_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %oe_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffeb_128B(<32 x i32> %o, <32 x i32> %e)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}


declare <32 x i32> @llvm.hexagon.V6.vshuffob_128B(<32 x i32>, <32 x i32>)
declare <32 x i32> @llvm.hexagon.V6.vshuffoh_128B(<32 x i32>, <32 x i32>)

define weak_odr <64 x i16> @halide.hexagon.trunclo.h(<64 x i32> %oe) nounwind uwtable readnone alwaysinline {
  %oe_32 = bitcast <64 x i32> %oe to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %oe_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %oe_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffoh_128B(<32 x i32> %o, <32 x i32> %e)
  %r = bitcast <32 x i32> %r_32 to <64 x i16>
  ret <64 x i16> %r
}

define weak_odr <128 x i8> @halide.hexagon.trunclo.b(<128 x i16> %oe) nounwind uwtable readnone alwaysinline {
  %oe_32 = bitcast <128 x i16> %oe to <64 x i32>
  %e = call <32 x i32> @llvm.hexagon.V6.lo_128B(<64 x i32> %oe_32)
  %o = call <32 x i32> @llvm.hexagon.V6.hi_128B(<64 x i32> %oe_32)
  %r_32 = tail call <32 x i32> @llvm.hexagon.V6.vshuffob_128B(<32 x i32> %o, <32 x i32> %e)
  %r = bitcast <32 x i32> %r_32 to <128 x i8>
  ret <128 x i8> %r
}

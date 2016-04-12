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

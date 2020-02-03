; We used to mark some of these as noinline, with the claim that
; "otherwise llvm sneakily peephole optimizes things like (float)(ceil((double)f)) -> ceilf(f),
; even though ceilf doesn't exist.""
;
; However, this causes another problem; since this module is always inlined into
; generated code by LLVM_Runtime_Linker, anything that is marked 'noinline' gets
; duplicated in each AOT section, making it impossible to link multiple AOT outputs
; into a single binary without using the /FORCE:MULTIPLE flag, which is suboptimal.
;
; Since posix_math.ll seems to be avoiding the peepholing issue mentioned above,
; we're marking everything in this file as alwaysinline (as expected by LLVM_Runtime_Linker)
; for now, and will re-examine the peephole issues if/when they appear.

declare double @llvm.sqrt.f64(double) nounwind readnone

define weak_odr double @sqrt_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.sqrt.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @sqrt_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @llvm.sin.f64(double) nounwind readnone

define weak_odr double @sin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.sin.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @sin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @sin_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @llvm.cos.f64(double) nounwind readnone

define weak_odr double @cos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.cos.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @cos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @cos_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @llvm.exp.f64(double) nounwind readnone

define weak_odr double @exp_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.exp.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @exp_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @exp_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @llvm.log.f64(double) nounwind readnone

define weak_odr double @log_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.log.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @log_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @log_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare float @llvm.fabs.f32(float) nounwind readnone
declare double @llvm.fabs.f64(double) nounwind readnone

define weak_odr double @abs_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.fabs.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @abs_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @abs_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @llvm.floor.f64(double) nounwind readnone

define weak_odr double @floor_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.floor.f64(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @floor_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @floor_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @ceil(double) nounwind readnone

define weak_odr double @ceil_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @ceil(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @ceil_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @ceil_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare float @llvm.nearbyint.f32(float) nounwind readnone
declare double @llvm.nearbyint.f64(double) nounwind readnone

define weak_odr float @round_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.nearbyint.f32(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @round_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.nearbyint.f64(double %x) nounwind readnone
       ret double %y
}

declare float @llvm.trunc.f32(float) nounwind readnone
declare double @llvm.trunc.f64(double) nounwind readnone

define weak_odr float @trunc_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.trunc.f32(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @trunc_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.trunc.f64(double %x) nounwind readnone
       ret double %y
}

declare double @llvm.pow.f64(double, double) nounwind readnone

define weak_odr double @pow_f64(double %x, double %y) nounwind uwtable readnone alwaysinline {
       %z = tail call double @llvm.pow.f64(double %x, double %y) nounwind readnone
       ret double %z
}

define weak_odr float @pow_f32(float %x1, float %x2) nounwind uwtable readnone alwaysinline {
       %x1d = fpext float %x1 to double
       %x2d = fpext float %x2 to double
       %yd = tail call double @pow_f64(double %x1d, double %x2d) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @asin(double) nounwind readnone

define weak_odr double @asin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @asin(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @asin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @asin_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @acos(double) nounwind readnone

define weak_odr double @acos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @acos(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @acos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @acos_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @tan(double) nounwind readnone

define weak_odr double @tan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @tan(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @tan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @tan_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @atan(double) nounwind readnone

define weak_odr double @atan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @atan(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @atan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @atan_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @atan2(double, double) nounwind readnone

define weak_odr double @atan2_f64(double %y, double %x) nounwind uwtable readnone alwaysinline {
       %z = tail call double @atan2(double %y, double %x) nounwind readnone
       ret double %z
}

define weak_odr float @atan2_f32(float %x1, float %x2) nounwind uwtable readnone alwaysinline {
       %x1d = fpext float %x1 to double
       %x2d = fpext float %x2 to double
       %yd = tail call double @atan2_f64(double %x1d, double %x2d) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @sinh(double) nounwind readnone

define weak_odr double @sinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @sinh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @sinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @sinh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @asinh(double) nounwind readnone

define weak_odr double @asinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @asinh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @asinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @asinh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @cosh(double) nounwind readnone

define weak_odr double @cosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @cosh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @cosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @cosh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @acosh(double) nounwind readnone

define weak_odr double @acosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @acosh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @acosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @acosh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @tanh(double) nounwind readnone

define weak_odr double @tanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @tanh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @tanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @tanh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}

declare double @atanh(double) nounwind readnone

define weak_odr double @atanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @atanh(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @atanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %xd = fpext float %x to double
       %yd = tail call double @atanh_f64(double %xd) nounwind readnone
       %y = fptrunc double %yd to float
       ret float %y
}


define weak_odr float @inf_f32() nounwind uwtable readnone alwaysinline {
       ret float 0x7FF0000000000000
}

define weak_odr float @neg_inf_f32() nounwind uwtable readnone alwaysinline {
       ret float 0xFFF0000000000000
}

define weak_odr float @nan_f32() nounwind uwtable readnone alwaysinline {
       ret float 0x7FF8000000000000
}


define weak_odr double @inf_f64() nounwind uwtable readnone alwaysinline {
       ret double 0x7FF0000000000000
}

define weak_odr double @neg_inf_f64() nounwind uwtable readnone alwaysinline {
       ret double 0xFFF0000000000000
}

define weak_odr double @nan_f64() nounwind uwtable readnone alwaysinline {
       ret double 0x7FF8000000000000
}
declare float @sqrtf(float) nounwind readnone
declare double @sqrt(double) nounwind readnone

define weak_odr float @sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @sqrtf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @sqrt_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @sqrt(double %x) nounwind readnone
       ret double %y
}

declare float @sinf(float) nounwind readnone
declare double @sin(double) nounwind readnone

define weak_odr float @sin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @sinf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @sin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @sin(double %x) nounwind readnone
       ret double %y
}

declare float @cosf(float) nounwind readnone
declare double @cos(double) nounwind readnone

define weak_odr float @cos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @cosf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @cos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @cos(double %x) nounwind readnone
       ret double %y
}

declare float @expf(float) nounwind readnone
declare double @exp(double) nounwind readnone

define weak_odr float @exp_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @expf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @exp_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @exp(double %x) nounwind readnone
       ret double %y
}

declare float @logf(float) nounwind readnone
declare double @log(double) nounwind readnone

define weak_odr float @log_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @logf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @log_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @log(double %x) nounwind readnone
       ret double %y
}

declare float @fabsf(float) nounwind readnone
declare double @fabs(double) nounwind readnone

define weak_odr float @abs_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @fabsf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @abs_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @fabs(double %x) nounwind readnone
       ret double %y
}

declare float @floorf(float) nounwind readnone
declare double @floor(double) nounwind readnone

define weak_odr float @floor_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @floorf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @floor_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @floor(double %x) nounwind readnone
       ret double %y
}

declare float @ceilf(float) nounwind readnone
declare double @ceil(double) nounwind readnone

define weak_odr float @ceil_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @ceilf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @ceil_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @ceil(double %x) nounwind readnone
       ret double %y
}

declare float @nearbyintf(float) nounwind readnone
declare double @nearbyint(double) nounwind readnone

define weak_odr float @round_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @nearbyintf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @round_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @nearbyint(double %x) nounwind readnone
       ret double %y
}

declare float @truncf(float) nounwind readnone
declare double @trunc(double) nounwind readnone

define weak_odr float @trunc_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @truncf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @trunc_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @trunc(double %x) nounwind readnone
       ret double %y
}

declare float @powf(float, float) nounwind readnone
declare double @pow(double, double) nounwind readnone

define weak_odr float @pow_f32(float %x, float %y) nounwind uwtable readnone alwaysinline {
       %z = tail call float @powf(float %x, float %y) nounwind readnone
       ret float %z
}

define weak_odr double @pow_f64(double %x, double %y) nounwind uwtable readnone alwaysinline {
       %z = tail call double @pow(double %x, double %y) nounwind readnone
       ret double %z
}

declare float @asinf(float) nounwind readnone
declare double @asin(double) nounwind readnone

define weak_odr float @asin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @asinf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @asin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @asin(double %x) nounwind readnone
       ret double %y
}

declare float @acosf(float) nounwind readnone
declare double @acos(double) nounwind readnone

define weak_odr float @acos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @acosf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @acos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @acos(double %x) nounwind readnone
       ret double %y
}

declare float @tanf(float) nounwind readnone
declare double @tan(double) nounwind readnone

define weak_odr float @tan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @tanf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @tan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @tan(double %x) nounwind readnone
       ret double %y
}

declare float @atanf(float) nounwind readnone
declare double @atan(double) nounwind readnone

define weak_odr float @atan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @atanf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @atan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @atan(double %x) nounwind readnone
       ret double %y
}

declare float @atan2f(float, float) nounwind readnone
declare double @atan2(double, double) nounwind readnone

define weak_odr float @atan2_f32(float %y, float %x) nounwind uwtable readnone alwaysinline {
       %z = tail call float @atan2f(float %y, float %x) nounwind readnone
       ret float %z
}

define weak_odr double @atan2_f64(double %y, double %x) nounwind uwtable readnone alwaysinline {
       %z = tail call double @atan2(double %y, double %x) nounwind readnone
       ret double %z
}

declare float @sinhf(float) nounwind readnone
declare double @sinh(double) nounwind readnone

define weak_odr float @sinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @sinhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @sinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @sinh(double %x) nounwind readnone
       ret double %y
}

declare float @asinhf(float) nounwind readnone
declare double @asinh(double) nounwind readnone

define weak_odr float @asinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @asinhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @asinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @asinh(double %x) nounwind readnone
       ret double %y
}

declare float @coshf(float) nounwind readnone
declare double @cosh(double) nounwind readnone

define weak_odr float @cosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @coshf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @cosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @cosh(double %x) nounwind readnone
       ret double %y
}

declare float @acoshf(float) nounwind readnone
declare double @acosh(double) nounwind readnone

define weak_odr float @acosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @acoshf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @acosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @acosh(double %x) nounwind readnone
       ret double %y
}

declare float @tanhf(float) nounwind readnone
declare double @tanh(double) nounwind readnone

define weak_odr float @tanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @tanhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @tanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @tanh(double %x) nounwind readnone
       ret double %y
}

declare float @atanhf(float) nounwind readnone
declare double @atanh(double) nounwind readnone

define weak_odr float @atanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @atanhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @atanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @atanh(double %x) nounwind readnone
       ret double %y
}
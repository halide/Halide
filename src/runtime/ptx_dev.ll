declare void @llvm.nvvm.barrier0()
declare  i32 @llvm.nvvm.read.ptx.sreg.tid.x()
declare  i32 @llvm.nvvm.read.ptx.sreg.ctaid.x()
declare  i32 @llvm.nvvm.read.ptx.sreg.ntid.x()
declare  i32 @llvm.nvvm.read.ptx.sreg.nctaid.x()
declare  i32 @llvm.nvvm.read.ptx.sreg.tid.y()
declare  i32 @llvm.nvvm.read.ptx.sreg.ctaid.y()
declare  i32 @llvm.nvvm.read.ptx.sreg.ntid.y()
declare  i32 @llvm.nvvm.read.ptx.sreg.nctaid.y()
declare  i32 @llvm.nvvm.read.ptx.sreg.tid.z()
declare  i32 @llvm.nvvm.read.ptx.sreg.ctaid.z()
declare  i32 @llvm.nvvm.read.ptx.sreg.ntid.z()
declare  i32 @llvm.nvvm.read.ptx.sreg.nctaid.z()
declare  i32 @llvm.nvvm.read.ptx.sreg.tid.w()
declare  i32 @llvm.nvvm.read.ptx.sreg.ctaid.w()
declare  i32 @llvm.nvvm.read.ptx.sreg.ntid.w()
declare  i32 @llvm.nvvm.read.ptx.sreg.nctaid.w()
declare  i32 @llvm.nvvm.read.ptx.sreg.warpsize()

; Remove these two once the minimum required llvm version is 9.0
declare float @llvm.nvvm.atomic.load.add.f32.p0f32(float*, float)
declare double @llvm.nvvm.atomic.load.add.f64.p0f64(double *, double)

; Legacy - to replace
;declare void @llvm.ptx.red.global.add.s32(i32*, i32)
;declare void @llvm.ptx.red.global.add.f32(float*, float)
;declare void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)*, i32)

define weak_odr float @nan_f32() nounwind uwtable readnone alwaysinline {
       ret float 0x7FF8000000000000;
}

define weak_odr float @neg_inf_f32() nounwind uwtable readnone alwaysinline {
       ret float 0xFFF0000000000000;
}

define weak_odr float @inf_f32() nounwind uwtable readnone alwaysinline {
       ret float 0x7FF0000000000000;
}

declare float @__nv_sqrtf(float) nounwind readnone
declare double @__nv_sqrt(double) nounwind readnone

define weak_odr float @sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_sqrtf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @sqrt_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_sqrt(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_frcp_rn(float) nounwind readnone

define weak_odr float @fast_inverse_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_frcp_rn(float %x) nounwind readnone
       ret float %y
}

declare float @llvm.nvvm.rsqrt.approx.ftz.f(float) nounwind readnone

define weak_odr float @fast_inverse_sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.nvvm.rsqrt.approx.ftz.f(float %x) nounwind readnone
       ret float %y
}

declare float @__nv_sinf(float) nounwind readnone
declare double @__nv_sin(double) nounwind readnone

define weak_odr float @sin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_sinf(float %x) nounwind readnone
       ret float %y
}

define weak_odr float @fast_sin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = call float asm "sin.approx.f32     $0, $1;", "=f,f" (float %x)
       ret float %y
}

define weak_odr double @sin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_sin(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_cosf(float) nounwind readnone
declare double @__nv_cos(double) nounwind readnone

define weak_odr float @cos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_cosf(float %x) nounwind readnone
       ret float %y
}

define weak_odr float @fast_cos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = call float asm "cos.approx.f32     $0, $1;", "=f,f" (float %x)
       ret float %y
}

define weak_odr double @cos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_cos(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_expf(float) nounwind readnone
declare double @__nv_exp(double) nounwind readnone

define weak_odr float @exp_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_expf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @exp_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_exp(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @fast_ex2_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = call float asm "ex2.approx.f32     $0, $1;", "=f,f" (float %x)
       ret float %y
}

declare float @__nv_logf(float) nounwind readnone
declare double @__nv_log(double) nounwind readnone

define weak_odr float @log_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_logf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @log_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_log(double %x) nounwind readnone
       ret double %y
}

define weak_odr float @fast_lg2_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = call float asm "lg2.approx.f32     $0, $1;", "=f,f" (float %x)
       ret float %y
}

declare float @__nv_fabsf(float) nounwind readnone
declare double @__nv_fabs(double) nounwind readnone

define weak_odr float @abs_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_fabsf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @abs_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_fabs(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_floorf(float) nounwind readnone
declare double @__nv_floor(double) nounwind readnone

define weak_odr float @floor_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_floorf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @floor_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_floor(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_ceilf(float) nounwind readnone
declare double @__nv_ceil(double) nounwind readnone

define weak_odr float @ceil_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_ceilf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @ceil_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_ceil(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_truncf(float) nounwind readnone
declare double @__nv_trunc(double) nounwind readnone

define weak_odr float @trunc_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_truncf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @trunc_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_trunc(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_powf(float, float) nounwind readnone
declare double @__nv_pow(double, double) nounwind readnone

define weak_odr float @pow_f32(float %x, float %y) nounwind uwtable readnone alwaysinline {
       %z = tail call float @__nv_powf(float %x, float %y) nounwind readnone
       ret float %z
}

define weak_odr double @pow_f64(double %x, double %y) nounwind uwtable readnone alwaysinline {
       %z = tail call double @__nv_pow(double %x, double %y) nounwind readnone
       ret double %z
}

declare float @__nv_asinf(float) nounwind readnone
declare double @__nv_asin(double) nounwind readnone

define weak_odr float @asin_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_asinf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @asin_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_asin(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_acosf(float) nounwind readnone
declare double @__nv_acos(double) nounwind readnone

define weak_odr float @acos_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_acosf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @acos_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_acos(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_tanf(float) nounwind readnone
declare double @__nv_tan(double) nounwind readnone

define weak_odr float @tan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_tanf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @tan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_tan(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_atanf(float) nounwind readnone
declare double @__nv_atan(double) nounwind readnone

define weak_odr float @atan_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_atanf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @atan_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_atan(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_atan2f(float, float) nounwind readnone
declare double @__nv_atan2(double, double) nounwind readnone

define weak_odr float @atan2_f32(float %y, float %x) nounwind uwtable readnone alwaysinline {
       %z = tail call float @__nv_atan2f(float %y, float %x) nounwind readnone
       ret float %z
}

define weak_odr double @atan2_f64(double %y, double %x) nounwind uwtable readnone alwaysinline {
       %z = tail call double @__nv_atan2(double %y, double %x) nounwind readnone
       ret double %z
}

declare float @__nv_sinhf(float) nounwind readnone
declare double @__nv_sinh(double) nounwind readnone

define weak_odr float @sinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_sinhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @sinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_sinh(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_asinhf(float) nounwind readnone
declare double @__nv_asinh(double) nounwind readnone

define weak_odr float @asinh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_asinhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @asinh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_asinh(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_coshf(float) nounwind readnone
declare double @__nv_cosh(double) nounwind readnone

define weak_odr float @cosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_coshf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @cosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_cosh(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_acoshf(float) nounwind readnone
declare double @__nv_acosh(double) nounwind readnone

define weak_odr float @acosh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_acoshf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @acosh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_acosh(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_tanhf(float) nounwind readnone
declare double @__nv_tanh(double) nounwind readnone

define weak_odr float @tanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_tanhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr float @fast_tanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       ; Requires SM75
       %y = call float asm "tanh.approx.f32     $0, $1;", "=f,f" (float %x)
       ret float %y
}

define weak_odr double @tanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_tanh(double %x) nounwind readnone
       ret double %y
}

declare float @__nv_atanhf(float) nounwind readnone
declare double @__nv_atanh(double) nounwind readnone

define weak_odr float @atanh_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_atanhf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @atanh_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_atanh(double %x) nounwind readnone
       ret double %y
}

define weak_odr i32 @halide_ptx_trap() nounwind uwtable alwaysinline {
       tail call void asm sideeffect "
       trap;
       ", ""() nounwind
       ret i32 0
}

; llvm doesn't expose dot product instructions as intrinsics
define weak_odr i32 @dp4a_s32_s32(<4 x i8> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i8> %a to i32
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp4a.s32.s32    $0, $1, $2, $3;", "=r,r,r,r"(i32 %a_32, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp4a_s32_u32(<4 x i8> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i8> %a to i32
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp4a.s32.u32    $0, $1, $2, $3;", "=r,r,r,r"(i32 %a_32, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp4a_u32_s32(<4 x i8> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i8> %a to i32
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp4a.u32.s32    $0, $1, $2, $3;", "=r,r,r,r"(i32 %a_32, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp4a_u32_u32(<4 x i8> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i8> %a to i32
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp4a.u32.u32    $0, $1, $2, $3;", "=r,r,r,r"(i32 %a_32, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}


define weak_odr i32 @dp2a_s32_s32(<4 x i16> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i16> %a to <2 x i32>
       %a_lo = extractelement <2 x i32> %a_32, i32 0
       %a_hi = extractelement <2 x i32> %a_32, i32 1
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp2a.lo.s32.s32    $0, $1, $3, $4; dp2a.hi.s32.s32    $0, $2, $3, $0;", "=r,r,r,r,r"(i32 %a_lo, i32 %a_hi, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp2a_s32_u32(<4 x i16> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i16> %a to <2 x i32>
       %a_lo = extractelement <2 x i32> %a_32, i32 0
       %a_hi = extractelement <2 x i32> %a_32, i32 1
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp2a.lo.s32.u32    $0, $1, $3, $4; dp2a.hi.s32.u32    $0, $2, $3, $0;", "=r,r,r,r,r"(i32 %a_lo, i32 %a_hi, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp2a_u32_s32(<4 x i16> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i16> %a to <2 x i32>
       %a_lo = extractelement <2 x i32> %a_32, i32 0
       %a_hi = extractelement <2 x i32> %a_32, i32 1
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp2a.lo.u32.s32    $0, $1, $3, $4; dp2a.hi.u32.s32    $0, $2, $3, $0;", "=r,r,r,r,r"(i32 %a_lo, i32 %a_hi, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}

define weak_odr i32 @dp2a_u32_u32(<4 x i16> %a, <4 x i8> %b, i32 %i) nounwind readnone alwaysinline {
       %a_32 = bitcast <4 x i16> %a to <2 x i32>
       %a_lo = extractelement <2 x i32> %a_32, i32 0
       %a_hi = extractelement <2 x i32> %a_32, i32 1
       %b_32 = bitcast <4 x i8> %b to i32
       %d = tail call i32 asm "dp2a.lo.u32.u32    $0, $1, $3, $4; dp2a.hi.u32.u32    $0, $2, $3, $0;", "=r,r,r,r,r"(i32 %a_lo, i32 %a_hi, i32 %b_32, i32 %i) nounwind readnone
       ret i32 %d
}


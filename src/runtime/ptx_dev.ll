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

declare float @__nv_nearbyintf(float) nounwind readnone
declare double @__nv_nearbyint(double) nounwind readnone

define weak_odr float @round_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @__nv_nearbyintf(float %x) nounwind readnone
       ret float %y
}

define weak_odr double @round_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @__nv_nearbyint(double %x) nounwind readnone
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

declare {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.a.row.f16.p0i8(i8 addrspace(0)* %src );
declare {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.a.row.stride.f16.p0i8(i8 addrspace(0)* %src , i32 %stride);
declare {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.b.row.f16.p0i8(i8 addrspace(0)* %src );
declare {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.b.row.stride.f16.p0i8(i8 addrspace(0)* %src , i32 %stride);
declare {float, float, float, float, float, float, float, float} @llvm.nvvm.wmma.m16n16k16.load.c.row.f32.p0i8(i8 addrspace(0)* %src );
declare {float, float, float, float, float, float, float, float} @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p0i8(i8 addrspace(0)* %src, i32 %stride );
declare {float, float, float, float, float, float, float, float} @llvm.nvvm.wmma.m16n16k16.mma.row.row.f32.f32(
        <2 x half> %a0, <2 x half> %a1, <2 x half> %a2, <2 x half> %a3, <2 x half> %a4, <2 x half> %a5, <2 x half> %a6, <2 x half> %a7,
        <2 x half> %b0, <2 x half> %b1, <2 x half> %b2, <2 x half> %b3, <2 x half> %b4, <2 x half> %b5, <2 x half> %b6, <2 x half> %b7,
        float %c0, float %c1, float %c2, float %c3, float %c4, float %c5, float %c6, float %c7);
declare void @llvm.nvvm.wmma.m16n16k16.store.d.row.f32.p0i8(i8 addrspace(0)* %src, float %d0, float %d1, float %d2, float %d3, float %d4, float %d5, float %d6, float %d7);
declare void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p0i8(i8 addrspace(0)* %src, float %d0, float %d1, float %d2, float %d3, float %d4, float %d5, float %d6, float %d7, i32 %stride);

define <16 x half> @wmma.m16n16k16.load.a.row.f16.p0i8(i8 addrspace(0)* %src, i32 %offset, i32 %stride) {
    %offset_bytes = mul i32 2, %offset
    %addr = getelementptr inbounds i8, i8 addrspace(0)* %src, i32 %offset_bytes
    %v0 = call {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.a.row.stride.f16.p0i8(i8 addrspace(0)* %addr, i32 %stride);

    %a0 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 0
    %a1 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 1
    %a2 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 2
    %a3 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 3
    %a4 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 4
    %a5 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 5
    %a6 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 6
    %a7 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 7

    %f0 = extractelement <2 x half> %a0, i32 0
    %f1 = extractelement <2 x half> %a0, i32 1
    %f2 = extractelement <2 x half> %a1, i32 0
    %f3 = extractelement <2 x half> %a1, i32 1
    %f4 = extractelement <2 x half> %a2, i32 0
    %f5 = extractelement <2 x half> %a2, i32 1
    %f6 = extractelement <2 x half> %a3, i32 0
    %f7 = extractelement <2 x half> %a3, i32 1
    %f8 = extractelement <2 x half> %a4, i32 0
    %f9 = extractelement <2 x half> %a4, i32 1
    %f10 = extractelement <2 x half> %a5, i32 0
    %f11 = extractelement <2 x half> %a5, i32 1
    %f12 = extractelement <2 x half> %a6, i32 0
    %f13 = extractelement <2 x half> %a6, i32 1
    %f14 = extractelement <2 x half> %a7, i32 0
    %f15 = extractelement <2 x half> %a7, i32 1

    %result_ptr = alloca <16 x half>, align 128
    %result = load <16 x half>, <16 x half>* %result_ptr

    %result0 = insertelement <16 x half> %result, half %f0, i32 0
    %result1 = insertelement <16 x half> %result0, half %f1, i32 1
    %result2 = insertelement <16 x half> %result1, half %f2, i32 2
    %result3 = insertelement <16 x half> %result2, half %f3, i32 3
    %result4 = insertelement <16 x half> %result3, half %f4, i32 4
    %result5 = insertelement <16 x half> %result4, half %f5, i32 5
    %result6 = insertelement <16 x half> %result5, half %f6, i32 6
    %result7 = insertelement <16 x half> %result6, half %f7, i32 7
    %result8 = insertelement <16 x half> %result7, half %f8, i32 8
    %result9 = insertelement <16 x half> %result8, half %f9, i32 9
    %result10 = insertelement <16 x half> %result9, half %f10, i32 10
    %result11 = insertelement <16 x half> %result10, half %f11, i32 11
    %result12 = insertelement <16 x half> %result11, half %f12, i32 12
    %result13 = insertelement <16 x half> %result12, half %f13, i32 13
    %result14 = insertelement <16 x half> %result13, half %f14, i32 14
    %result15 = insertelement <16 x half> %result14, half %f15, i32 15

    ret <16 x half> %result15
}

define <16 x half> @wmma.m16n16k16.load.b.row.f16.p0i8(i8 addrspace(0)* %src, i32 %offset, i32 %stride) {
    %offset_bytes = mul i32 2, %offset
    %addr = getelementptr inbounds i8, i8 addrspace(0)* %src, i32 %offset_bytes
    %v0 = call {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} @llvm.nvvm.wmma.m16n16k16.load.b.row.stride.f16.p0i8(i8 addrspace(0)* %addr, i32 %stride);

    %a0 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 0
    %a1 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 1
    %a2 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 2
    %a3 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 3
    %a4 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 4
    %a5 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 5
    %a6 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 6
    %a7 = extractvalue {<2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>, <2 x half>} %v0, 7

    %f0 = extractelement <2 x half> %a0, i32 0
    %f1 = extractelement <2 x half> %a0, i32 1
    %f2 = extractelement <2 x half> %a1, i32 0
    %f3 = extractelement <2 x half> %a1, i32 1
    %f4 = extractelement <2 x half> %a2, i32 0
    %f5 = extractelement <2 x half> %a2, i32 1
    %f6 = extractelement <2 x half> %a3, i32 0
    %f7 = extractelement <2 x half> %a3, i32 1
    %f8 = extractelement <2 x half> %a4, i32 0
    %f9 = extractelement <2 x half> %a4, i32 1
    %f10 = extractelement <2 x half> %a5, i32 0
    %f11 = extractelement <2 x half> %a5, i32 1
    %f12 = extractelement <2 x half> %a6, i32 0
    %f13 = extractelement <2 x half> %a6, i32 1
    %f14 = extractelement <2 x half> %a7, i32 0
    %f15 = extractelement <2 x half> %a7, i32 1

    %result_ptr = alloca <16 x half>, align 128
    %result = load <16 x half>, <16 x half>* %result_ptr

    %result0 = insertelement <16 x half> %result, half %f0, i32 0
    %result1 = insertelement <16 x half> %result0, half %f1, i32 1
    %result2 = insertelement <16 x half> %result1, half %f2, i32 2
    %result3 = insertelement <16 x half> %result2, half %f3, i32 3
    %result4 = insertelement <16 x half> %result3, half %f4, i32 4
    %result5 = insertelement <16 x half> %result4, half %f5, i32 5
    %result6 = insertelement <16 x half> %result5, half %f6, i32 6
    %result7 = insertelement <16 x half> %result6, half %f7, i32 7
    %result8 = insertelement <16 x half> %result7, half %f8, i32 8
    %result9 = insertelement <16 x half> %result8, half %f9, i32 9
    %result10 = insertelement <16 x half> %result9, half %f10, i32 10
    %result11 = insertelement <16 x half> %result10, half %f11, i32 11
    %result12 = insertelement <16 x half> %result11, half %f12, i32 12
    %result13 = insertelement <16 x half> %result12, half %f13, i32 13
    %result14 = insertelement <16 x half> %result13, half %f14, i32 14
    %result15 = insertelement <16 x half> %result14, half %f15, i32 15

    ret <16 x half> %result15
}

define <8 x float> @wmma.m16n16k16.load.c.row.f32.p0i8(i8 addrspace(0)* %src, i32 %offset, i32 %stride) {
    %offset_bytes = mul i32 4, %offset
    %addr = getelementptr inbounds i8, i8 addrspace(0)* %src, i32 %offset_bytes
    %v2 = call {float, float, float, float, float, float, float, float} @llvm.nvvm.wmma.m16n16k16.load.c.row.stride.f32.p0i8(i8 addrspace(0)* %addr, i32 %stride)

    %c0 = extractvalue {float, float, float, float, float, float, float, float} %v2, 0
    %c1 = extractvalue {float, float, float, float, float, float, float, float} %v2, 1
    %c2 = extractvalue {float, float, float, float, float, float, float, float} %v2, 2
    %c3 = extractvalue {float, float, float, float, float, float, float, float} %v2, 3
    %c4 = extractvalue {float, float, float, float, float, float, float, float} %v2, 4
    %c5 = extractvalue {float, float, float, float, float, float, float, float} %v2, 5
    %c6 = extractvalue {float, float, float, float, float, float, float, float} %v2, 6
    %c7 = extractvalue {float, float, float, float, float, float, float, float} %v2, 7

    %result_ptr = alloca <8 x float>, align 128
    %result = load <8 x float>, <8 x float>* %result_ptr

    %result0 = insertelement <8 x float> %result, float %c0, i32 0
    %result1 = insertelement <8 x float> %result0, float %c1, i32 1
    %result2 = insertelement <8 x float> %result1, float %c2, i32 2
    %result3 = insertelement <8 x float> %result2, float %c3, i32 3
    %result4 = insertelement <8 x float> %result3, float %c4, i32 4
    %result5 = insertelement <8 x float> %result4, float %c5, i32 5
    %result6 = insertelement <8 x float> %result5, float %c6, i32 6
    %result7 = insertelement <8 x float> %result6, float %c7, i32 7

    ret <8 x float> %result7
}

define weak_odr <2 x half> @extract_wmma_half_fragment(<16 x half> %v, i32 %idx1, i32 %idx2) nounwind readnone alwaysinline {
    %a = extractelement <16 x half> %v, i32 %idx1
    %b = extractelement <16 x half> %v, i32 %idx2
    %frag_ptr = alloca <2 x half>, align 128
    %frag = load <2 x half>, <2 x half>* %frag_ptr
    %frag_0 = insertelement <2 x half> %frag, half %a, i32 0
    %frag_1 = insertelement <2 x half> %frag_0, half %b, i32 1
    ret <2 x half> %frag_1
}

define weak_odr <8 x float> @wmma.m16n16k16.mma.row.row.f32.f32(<16 x half> %a, <16 x half> %b, <8 x float> %c) nounwind readnone alwaysinline {
    %a0 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 0, i32 1)
    %a1 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 2, i32 3)
    %a2 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 4, i32 5)
    %a3 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 6, i32 7)
    %a4 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 8, i32 9)
    %a5 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 10, i32 11)
    %a6 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 12, i32 13)
    %a7 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %a, i32 14, i32 15)

    %b0 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 0, i32 1)
    %b1 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 2, i32 3)
    %b2 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 4, i32 5)
    %b3 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 6, i32 7)
    %b4 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 8, i32 9)
    %b5 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 10, i32 11)
    %b6 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 12, i32 13)
    %b7 = call <2 x half> @extract_wmma_half_fragment(<16 x half> %b, i32 14, i32 15)

    %c0 = extractelement <8 x float> %c, i32 0
    %c1 = extractelement <8 x float> %c, i32 1
    %c2 = extractelement <8 x float> %c, i32 2
    %c3 = extractelement <8 x float> %c, i32 3
    %c4 = extractelement <8 x float> %c, i32 4
    %c5 = extractelement <8 x float> %c, i32 5
    %c6 = extractelement <8 x float> %c, i32 6
    %c7 = extractelement <8 x float> %c, i32 7

    %v3 = call {float, float, float, float, float, float, float, float} @llvm.nvvm.wmma.m16n16k16.mma.row.row.f32.f32(
        <2 x half> %a0, <2 x half> %a1, <2 x half> %a2, <2 x half> %a3, <2 x half> %a4, <2 x half> %a5, <2 x half> %a6, <2 x half> %a7,
        <2 x half> %b0, <2 x half> %b1, <2 x half> %b2, <2 x half> %b3, <2 x half> %b4, <2 x half> %b5, <2 x half> %b6, <2 x half> %b7,
        float %c0, float %c1, float %c2, float %c3, float %c4, float %c5, float %c6, float %c7)

    %d0 = extractvalue {float, float, float, float, float, float, float, float} %v3, 0
    %d1 = extractvalue {float, float, float, float, float, float, float, float} %v3, 1
    %d2 = extractvalue {float, float, float, float, float, float, float, float} %v3, 2
    %d3 = extractvalue {float, float, float, float, float, float, float, float} %v3, 3
    %d4 = extractvalue {float, float, float, float, float, float, float, float} %v3, 4
    %d5 = extractvalue {float, float, float, float, float, float, float, float} %v3, 5
    %d6 = extractvalue {float, float, float, float, float, float, float, float} %v3, 6
    %d7 = extractvalue {float, float, float, float, float, float, float, float} %v3, 7

    %result_ptr = alloca <8 x float>, align 128
    %result0 = load <8 x float>, <8 x float>* %result_ptr
    %result1 = insertelement <8 x float> %result0, float %d0, i32 0
    %result2 = insertelement <8 x float> %result1, float %d1, i32 1
    %result3 = insertelement <8 x float> %result2, float %d2, i32 2
    %result4 = insertelement <8 x float> %result3, float %d3, i32 3
    %result5 = insertelement <8 x float> %result4, float %d4, i32 4
    %result6 = insertelement <8 x float> %result5, float %d5, i32 5
    %result7 = insertelement <8 x float> %result6, float %d6, i32 6
    %result8 = insertelement <8 x float> %result7, float %d7, i32 7

    ret <8 x float> %result8
}

define weak_odr i8 addrspace(0)* @wmma.m16n16k16.store.d.row.f32(i8 addrspace(0)* %d, i32 %offset, i32 %stride, <8 x float> %frag) nounwind {
    %d0 = extractelement <8 x float> %frag, i32 0
    %d1 = extractelement <8 x float> %frag, i32 1
    %d2 = extractelement <8 x float> %frag, i32 2
    %d3 = extractelement <8 x float> %frag, i32 3
    %d4 = extractelement <8 x float> %frag, i32 4
    %d5 = extractelement <8 x float> %frag, i32 5
    %d6 = extractelement <8 x float> %frag, i32 6
    %d7 = extractelement <8 x float> %frag, i32 7

    %offset_bytes = mul i32 4, %offset
    %addr = getelementptr inbounds i8, i8 addrspace(0)* %d, i32 %offset_bytes
    call void @llvm.nvvm.wmma.m16n16k16.store.d.row.stride.f32.p0i8(i8 addrspace(0)* %addr, float %d0, float %d1, float %d2, float %d3, float %d4, float %d5, float %d6, float %d7, i32 %stride);

    ret i8 addrspace(0)* %addr
}

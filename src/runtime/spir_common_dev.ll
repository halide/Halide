declare spir_func float @_Z4fabsf(float) nounwind readnone
declare spir_func float @_Z4sqrtf(float) nounwind readnone
declare spir_func float @_Z3sinf(float) nounwind readnone
declare spir_func float @_Z3cosf(float) nounwind readnone
declare spir_func float @_Z3expf(float) nounwind readnone
declare spir_func float @_Z3logf(float) nounwind readnone
declare spir_func float @_Z5floorf(float) nounwind readnone
declare spir_func float @_Z4ceilf(float) nounwind readnone
declare spir_func float @_Z5roundf(float) nounwind readnone
declare spir_func float @_Z3powff(float, float) nounwind readnone
declare spir_func float @_Z4asinf(float) nounwind readnone
declare spir_func float @_Z4acosf(float) nounwind readnone
declare spir_func float @_Z3tanf(float) nounwind readnone
declare spir_func float @_Z4atanf(float) nounwind readnone
declare spir_func float @_Z5atan2ff(float, float) nounwind readnone
declare spir_func float @_Z4sinhf(float) nounwind readnone
declare spir_func float @_Z5asinhf(float) nounwind readnone
declare spir_func float @_Z4coshf(float) nounwind readnone
declare spir_func float @_Z5acoshf(float) nounwind readnone
declare spir_func float @_Z4tanhf(float) nounwind readnone
declare spir_func float @_Z5atanhf(float) nounwind readnone

declare spir_func float @_Z11native_sqrtf(float) nounwind readnone
declare spir_func float @_Z10native_sinf(float) nounwind readnone
declare spir_func float @_Z10native_cosf(float) nounwind readnone

define weak_odr spir_func float @nan_f32() nounwind uwtable readnone alwaysinline {
    ; llvm uses 64-bit hex constants to represent floats and doubles alike.
    ret float 0x7FF8000000000000
}
define weak_odr spir_func float @neg_inf_f32() nounwind uwtable readnone alwaysinline {
    ret float 0xFFF0000000000000
}
define weak_odr spir_func float @inf_f32() nounwind uwtable readnone alwaysinline {
    ret float 0x7FF0000000000000
}

define weak_odr spir_func float @floor_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z5floorf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @ceil_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4ceilf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @sqrt_f32(float %x) nounwind uwtable readnone alwaysinline {
    %res = tail call float @_Z4sqrtf(float %x) nounwind readnone
    ret float %res
}
define weak_odr spir_func float @sin_f32(float %x) nounwind uwtable readnone alwaysinline {
    %res = tail call float @_Z3sinf(float %x) nounwind readnone
    ret float %res
}
define weak_odr spir_func float @cos_f32(float %x) nounwind uwtable readnone alwaysinline {
    %res = tail call float @_Z3cosf(float %x) nounwind readnone
    ret float %res
}
define weak_odr spir_func float @abs_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4fabsf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @exp_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z3expf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @log_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z3logf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @round_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z5roundf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @pow_f32(float %x, float %y) nounwind uwtable readnone alwaysinline {
    %z = tail call float @_Z3powff(float %x, float %y) nounwind readnone
    ret float %z
}
define weak_odr spir_func float @asin_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4asinf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @acos_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4acosf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @tan_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z3tanf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @atan_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4atanf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @atan2_f32(float %y, float %x) nounwind uwtable readnone alwaysinline {
    %z = tail call float @_Z5atan2ff(float %y, float %x) nounwind readnone
    ret float %z
}
define weak_odr spir_func float @sinh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4sinhf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @asinh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z5asinhf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @cosh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4coshf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @acosh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z5acoshf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @tanh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z4tanhf(float %x) nounwind readnone
    ret float %y
}
define weak_odr spir_func float @atanh_f32(float %x) nounwind uwtable readnone alwaysinline {
    %y = tail call float @_Z5atanhf(float %x) nounwind readnone
    ret float %y
}

; Dummy kernel to avoid errors on implementations that assert at least one kernel
; is in the program.
define spir_kernel void @_at_least_one_kernel(i32 %x) {
entry:
    ret void
}

!4 = metadata !{metadata !"kernel_arg_addr_space", i32 0}
!5 = metadata !{metadata !"kernel_arg_access_qual", metadata !"none"}
!6 = metadata !{metadata !"kernel_arg_type", metadata !"int"}
!7 = metadata !{metadata !"kernel_arg_type_qual", metadata !""}
!8 = metadata !{metadata !"kernel_arg_name", metadata !"x"}
!3 = metadata !{void (i32)* @_at_least_one_kernel, metadata !4, metadata !5, metadata !6, metadata !7, metadata !8}
!opencl.kernels = !{!3}

!0 = metadata !{i32 1, i32 2}
!1 = metadata !{i32 1, i32 0}
!2 = metadata !{}

!opencl.enable.FP_CONTRACT = !{}
!opencl.spir.version = !{!0}
!opencl.ocl.version = !{!1}
!opencl.used.extensions = !{!2}
!opencl.used.optional.core.features = !{!2}
!opencl.compiler.options = !{!2}

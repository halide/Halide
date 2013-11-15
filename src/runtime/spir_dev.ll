declare spir_func void @_Z7barrierj(i32)
declare spir_func i64 @_Z12get_local_idj(i32) nounwind readnone
declare spir_func i64 @_Z14get_local_sizej(i32) nounwind readnone
declare spir_func i64 @_Z12get_group_idj(i32) nounwind readnone
declare spir_func i64 @_Z14get_num_groupsj(i32) nounwind readnone

declare spir_func float @_Z5floorf(float) nounwind readnone
declare spir_func float @_Z4ceilf(float) nounwind readnone
declare spir_func float @_Z4sqrtf(float) nounwind readnone
declare spir_func float @_Z3sinf(float) nounwind readnone
declare spir_func float @_Z3cosf(float) nounwind readnone
declare spir_func float @_Z4fabsf(float) nounwind readnone

declare spir_func float @_Z11native_sqrtf(float) nounwind readnone
declare spir_func float @_Z10native_sinf(float) nounwind readnone
declare spir_func float @_Z10native_cosf(float) nounwind readnone

define weak_odr spir_func void @halide.spir.barrier() alwaysinline {
	; CLK_LOCAL_MEM_FENCE = 1, CLK_GLOBAL_MEM_FENCE = 2
	call void @_Z7barrierj(i32 3)
	ret void
}

define weak_odr spir_func i32 @halide.spir.lid.x() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_local_idj(i32 0) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.lid.y() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_local_idj(i32 1) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.lid.z() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_local_idj(i32 2) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}

define weak_odr spir_func i32 @halide.spir.lsz.x() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_local_sizej(i32 0) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.lsz.y() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_local_sizej(i32 1) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.lsz.z() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_local_sizej(i32 2) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}

define weak_odr spir_func i32 @halide.spir.gid.x() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_group_idj(i32 0) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.gid.y() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_group_idj(i32 1) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.gid.z() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z12get_group_idj(i32 2) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}

define weak_odr spir_func i32 @halide.spir.gsz.x() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_num_groupsj(i32 0) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.gsz.y() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_num_groupsj(i32 1) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
}
define weak_odr spir_func i32 @halide.spir.gsz.z() nounwind uwtable readnone alwaysinline {
    %x = tail call i64 @_Z14get_num_groupsj(i32 2) nounwind readnone
	%y = trunc i64 %x to i32
    ret i32 %y
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

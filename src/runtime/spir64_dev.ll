target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024"
target triple = "spir64-unknown-unknown"

declare spir_func void @_Z7barrierj(i32)

define weak_odr spir_func void @halide.spir.barrier() alwaysinline {
    ; CLK_LOCAL_MEM_FENCE = 1, CLK_GLOBAL_MEM_FENCE = 2
    call void @_Z7barrierj(i32 3)
    ret void
}

declare spir_func i64 @_Z12get_local_idj(i32) nounwind readnone
declare spir_func i64 @_Z14get_local_sizej(i32) nounwind readnone
declare spir_func i64 @_Z12get_group_idj(i32) nounwind readnone
declare spir_func i64 @_Z14get_num_groupsj(i32) nounwind readnone


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

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

; Legacy - to replace
;declare void @llvm.ptx.red.global.add.s32(i32*, i32)
;declare void @llvm.ptx.red.global.add.f32(float*, float)
;declare void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)*, i32)

declare float @llvm.nvvm.floor.f(float) nounwind readnone
declare float @llvm.nvvm.ceil.f(float) nounwind readnone
declare float @llvm.nvvm.sqrt.f(float) nounwind readnone
declare float @llvm.nvvm.sin.approx.f(float) nounwind readnone
declare float @llvm.nvvm.cos.approx.f(float) nounwind readnone

define internal ptx_device float @floor_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.nvvm.floor.f(float %x) nounwind readnone
       ret float %y
}

define internal ptx_device float @ceil_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.nvvm.ceil.f(float %x) nounwind readnone
       ret float %y
}

define internal ptx_device float @sqrt_f32(float %x) alwaysinline {
    %res = call float @llvm.nvvm.sqrt.f(float %x)
    ret float %res
}

define internal ptx_device float @sin_f32(float %x) alwaysinline {
    %res = call float @llvm.nvvm.sin.approx.f(float %x)
    ret float %res
}

define internal ptx_device float @cos_f32(float %x) alwaysinline {
    %res = call float @llvm.nvvm.cos.approx.f(float %x)
    ret float %res
}


declare float @llvm.nvvm.fabs.f(float) nounwind readnone
declare double @llvm.nvvm.fabs.d(double) nounwind readnone

define internal float @abs_f32(float %x) nounwind uwtable readnone alwaysinline {
       %y = tail call float @llvm.nvvm.fabs.f(float %x) nounwind readnone
       ret float %y
}

define internal double @abs_f64(double %x) nounwind uwtable readnone alwaysinline {
       %y = tail call double @llvm.nvvm.fabs.d(double %x) nounwind readnone
       ret double %y
}

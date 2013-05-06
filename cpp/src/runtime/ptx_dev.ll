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

declare float @llvm.sin.f32(float)
declare float @llvm.cos.f32(float)
declare float @llvm.sqrt.f32(float)

define internal ptx_device float @sqrt_f32(float %x) alwaysinline {
    %res = call float @llvm.sqrt.f32(float %x)
    ret float %res
}

define internal ptx_device float @sin_f32(float %x) alwaysinline {
    %res = call float @llvm.sin.f32(float %x)
    ret float %res
}

define internal ptx_device float @cos_f32(float %x) alwaysinline {
    %res = call float @llvm.cos.f32(float %x)
    ret float %res
}

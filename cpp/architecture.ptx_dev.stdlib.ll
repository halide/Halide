declare void @llvm.ptx.bar.sync(i32)
declare i32 @llvm.ptx.read.tid.x()
declare i32 @llvm.ptx.read.ctaid.x()
declare i32 @llvm.ptx.read.ntid.x()
declare i32 @llvm.ptx.read.nctaid.x()
declare i32 @llvm.ptx.read.tid.y()
declare i32 @llvm.ptx.read.ctaid.y()
declare i32 @llvm.ptx.read.ntid.y()
declare i32 @llvm.ptx.read.nctaid.y()
declare i32 @llvm.ptx.read.tid.z()
declare i32 @llvm.ptx.read.ctaid.z()
declare i32 @llvm.ptx.read.ntid.z()
declare i32 @llvm.ptx.read.nctaid.z()
declare i32 @llvm.ptx.read.tid.w()
declare i32 @llvm.ptx.read.ctaid.w()
declare i32 @llvm.ptx.read.ntid.w()
declare i32 @llvm.ptx.read.nctaid.w()
declare i32 @llvm.ptx.read.laneid()
declare i32 @llvm.ptx.read.warpid()
declare i32 @llvm.ptx.read.nwarpid()
declare i32 @llvm.ptx.read.smid()
declare i32 @llvm.ptx.read.nsmid()
declare i32 @llvm.ptx.read.gridid()
declare i32 @llvm.ptx.read.clock()
declare i64 @llvm.ptx.read.clock64()
declare i32 @llvm.ptx.read.pm0()
declare i32 @llvm.ptx.read.pm1()
declare i32 @llvm.ptx.read.pm2()
declare i32 @llvm.ptx.read.pm3()

; Legacy - to replace
;declare void @llvm.ptx.red.global.add.s32(i32*, i32)
;declare void @llvm.ptx.red.global.add.f32(float*, float)
;declare void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)*, i32)

declare float @llvm.sin.f32(float)
declare float @llvm.cos.f32(float)
declare float @llvm.sqrt.f32(float)

define ptx_device float @sqrt_f32(float %x) alwaysinline {
    %res = call float @llvm.sqrt.f32(float %x)
    ret float %res
}

define ptx_device float @sin_f32(float %x) alwaysinline {
    %res = call float @llvm.sin.f32(float %x)
    ret float %res
}

define ptx_device float @cos_f32(float %x) alwaysinline {
    %res = call float @llvm.cos.f32(float %x)
    ret float %res
}

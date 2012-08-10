; ModuleID = '<halide-device-ptx>'

declare void @llvm.ptx.bar.sync(i32) nounwind

declare i32 @llvm.ptx.read.tid.x() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.x() nounwind readnone

declare i32 @llvm.ptx.read.ntid.x() nounwind readnone

declare i32 @llvm.ptx.read.nctaid.x() nounwind readnone

declare i32 @llvm.ptx.read.tid.y() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.y() nounwind readnone

declare i32 @llvm.ptx.read.ntid.y() nounwind readnone

declare i32 @llvm.ptx.read.nctaid.y() nounwind readnone

declare i32 @llvm.ptx.read.tid.z() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.z() nounwind readnone

declare i32 @llvm.ptx.read.ntid.z() nounwind readnone

declare i32 @llvm.ptx.read.nctaid.z() nounwind readnone

declare i32 @llvm.ptx.read.tid.w() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.w() nounwind readnone

declare i32 @llvm.ptx.read.ntid.w() nounwind readnone

declare i32 @llvm.ptx.read.nctaid.w() nounwind readnone

declare i32 @llvm.ptx.read.laneid() nounwind readnone

declare i32 @llvm.ptx.read.warpid() nounwind readnone

declare i32 @llvm.ptx.read.nwarpid() nounwind readnone

declare i32 @llvm.ptx.read.smid() nounwind readnone

declare i32 @llvm.ptx.read.nsmid() nounwind readnone

declare i32 @llvm.ptx.read.gridid() nounwind readnone

declare i32 @llvm.ptx.read.clock() nounwind readnone

declare i64 @llvm.ptx.read.clock64() nounwind readnone

declare i32 @llvm.ptx.read.pm0() nounwind readnone

declare i32 @llvm.ptx.read.pm1() nounwind readnone

declare i32 @llvm.ptx.read.pm2() nounwind readnone

declare i32 @llvm.ptx.read.pm3() nounwind readnone

declare void @llvm.ptx.red.global.add.s32(i32*, i32) nounwind

declare void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)*, i32) nounwind

declare float @llvm.sin.f32(float) nounwind readonly

declare float @llvm.cos.f32(float) nounwind readonly

declare float @llvm.sqrt.f32(float) nounwind readonly

define ptx_kernel void @kernel(i8* %.i0, i8* %.result) {
entry:
  %stack = alloca i64

  %memref_elem_ptr = bitcast i8* %.result to i32*
  %memref_elem_ptr5 = bitcast i8* %.i0 to i32*

  ; breaks:
  %memref = getelementptr i32* %memref_elem_ptr5, i32 0;%9
  %idx = load i32* %memref

  ; works 1: upcast first and store to stack - causes sext %idx to move to a vreg before the load I think
  ;%idx64 = sext i32 %idx to i64
  ;store i64 %idx64, i64* %stack
  ;%memref6 = getelementptr i32* %memref_elem_ptr, i64 %idx64 ;i32 %idx

  ; works 2: load a 64 bit value directly from memory and use that as the offset
  ;%memref32 = getelementptr i32* %memref_elem_ptr5, i32 0;%9
  ;%memref = bitcast i32* %memref32 to i64*
  ;%idx = load i64* %memref
  ;%memref6 = getelementptr i32* %memref_elem_ptr, i64 %idx64

  %memref6 = getelementptr i32* %memref_elem_ptr, i32 %idx

  store i32 1, i32* %memref6
  ; this works - i will push these upstream soon later:
  ;call void @llvm.ptx.red.global.add.s32(i32* %memref6, i32 1)

  ret void

;hist.p0.threadidx_simt_afterloop:                 ; preds = %hist.p0.threadidx_simt_loop, %hist.p0.threadidy_simt_loop
;  br label %hist.p0.threadidy_simt_afterloop
}

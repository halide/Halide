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
;  %extern_llvm.ptx.read.ctaid.y = call i32 @llvm.ptx.read.ctaid.y()
;  %0 = icmp slt i32 %extern_llvm.ptx.read.ctaid.y, 10
;  br i1 %0, label %hist.p0.blockidy_simt_loop, label %hist.p0.blockidy_simt_afterloop
;
;hist.p0.blockidy_simt_loop:                       ; preds = %entry
;  %extern_llvm.ptx.read.ctaid.y1 = call i32 @llvm.ptx.read.ctaid.y()
;  %hist.p0.blockidy = add i32 %extern_llvm.ptx.read.ctaid.y1, 0
;  %extern_llvm.ptx.read.ctaid.x = call i32 @llvm.ptx.read.ctaid.x()
;  %1 = icmp slt i32 %extern_llvm.ptx.read.ctaid.x, 10
;  br i1 %1, label %hist.p0.blockidx_simt_loop, label %hist.p0.blockidx_simt_afterloop
;
;hist.p0.blockidy_simt_afterloop:                  ; preds = %hist.p0.blockidx_simt_afterloop, %entry
;  ret void
;
;hist.p0.blockidx_simt_loop:                       ; preds = %hist.p0.blockidy_simt_loop
;  %extern_llvm.ptx.read.ctaid.x2 = call i32 @llvm.ptx.read.ctaid.x()
;  %hist.p0.blockidx = add i32 %extern_llvm.ptx.read.ctaid.x2, 0
;  %extern_llvm.ptx.read.tid.y = call i32 @llvm.ptx.read.tid.y()
;  %2 = icmp slt i32 %extern_llvm.ptx.read.tid.y, 10
;  br i1 %2, label %hist.p0.threadidy_simt_loop, label %hist.p0.threadidy_simt_afterloop
;
;hist.p0.blockidx_simt_afterloop:                  ; preds = %hist.p0.threadidy_simt_afterloop, %hist.p0.blockidy_simt_loop
;  br label %hist.p0.blockidy_simt_afterloop
;
;hist.p0.threadidy_simt_loop:                      ; preds = %hist.p0.blockidx_simt_loop
;  %extern_llvm.ptx.read.tid.y3 = call i32 @llvm.ptx.read.tid.y()
;  %hist.p0.threadidy = add i32 %extern_llvm.ptx.read.tid.y3, 0
;  %extern_llvm.ptx.read.tid.x = call i32 @llvm.ptx.read.tid.x()
;  %3 = icmp slt i32 %extern_llvm.ptx.read.tid.x, 10
;  br i1 %3, label %hist.p0.threadidx_simt_loop, label %hist.p0.threadidx_simt_afterloop
;
;hist.p0.threadidy_simt_afterloop:                 ; preds = %hist.p0.threadidx_simt_afterloop, %hist.p0.blockidx_simt_loop
;  br label %hist.p0.blockidx_simt_afterloop
;
;hist.p0.threadidx_simt_loop:                      ; preds = %hist.p0.threadidy_simt_loop
  %extern_llvm.ptx.read.tid.x4 = call i32 @llvm.ptx.read.tid.x()
  %hist.p0.threadidx = add i32 %extern_llvm.ptx.read.tid.x4, 0
  %memref_elem_ptr = bitcast i8* %.result to i32*
  %memref_elem_ptr5 = bitcast i8* %.i0 to i32*
  %4 = mul i32 %hist.p0.blockidy, 10
  %5 = add i32 %4, %hist.p0.threadidy
  %6 = mul i32 %5, 100
  %7 = mul i32 %hist.p0.blockidx, 10
  %8 = add i32 %7, %hist.p0.threadidx
  %9 = add i32 %8, %6
  %memref = getelementptr i32* %memref_elem_ptr5, i32 %9
  %10 = load i32* %memref
  %memref6 = getelementptr i32* %memref_elem_ptr, i32 %10
  %memref_elem_ptr7 = bitcast i8* %.result to i32*
  %memref_elem_ptr8 = bitcast i8* %.i0 to i32*
  %11 = mul i32 %hist.p0.blockidy, 10
  %12 = add i32 %11, %hist.p0.threadidy
  %13 = mul i32 %12, 100
  %14 = mul i32 %hist.p0.blockidx, 10
  %15 = add i32 %14, %hist.p0.threadidx
  %16 = add i32 %15, %13
  %memref9 = getelementptr i32* %memref_elem_ptr8, i32 %16
  %17 = load i32* %memref9
  %memref10 = getelementptr i32* %memref_elem_ptr7, i32 %17
  %18 = load i32* %memref10
  %19 = add i32 %18, 1
  call void @llvm.ptx.red.global.add.s32(i32* %memref6, i32 %19)
  br label %hist.p0.threadidx_simt_afterloop

hist.p0.threadidx_simt_afterloop:                 ; preds = %hist.p0.threadidx_simt_loop, %hist.p0.threadidy_simt_loop
  br label %hist.p0.threadidy_simt_afterloop
}

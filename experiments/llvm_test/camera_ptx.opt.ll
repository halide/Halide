; ModuleID = '<stdin>'

declare i32 @llvm.ptx.read.tid.x() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.x() nounwind readnone

declare i32 @llvm.ptx.read.tid.y() nounwind readnone

declare i32 @llvm.ptx.read.ctaid.y() nounwind readnone

define ptx_kernel void @kernel(i8* nocapture %.raw, i32 %.raw.dim.0, i32 %.raw.dim.1, i8* nocapture %.result, i32 %.result.dim.0, i32 %.result.dim.1, i32 %.result.dim.2, i8* nocapture %output.curved.corrected.matrix, i32 %output.curved.corrected.matrix.y.min, i8* nocapture %output.curved.curve, i32 %output.nilc) nounwind {
entry:
  %0 = alloca [256 x <4 x i32>]
  %output.curved.corrected.dem.f9 = bitcast [256 x <4 x i32>]* %0 to i16*
  %1 = alloca [64 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8 = bitcast [64 x <4 x i32>]* %1 to i16*
  %2 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f7 = bitcast [32 x <4 x i32>]* %2 to i16*
  %3 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f7.b_r = bitcast [16 x <4 x i32>]* %3 to i16*
  %4 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f7.b_gr = bitcast [16 x <4 x i32>]* %4 to i16*
  %5 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f6 = bitcast [32 x <4 x i32>]* %5 to i16*
  %6 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f6.b_gb = bitcast [16 x <4 x i32>]* %6 to i16*
  %7 = alloca [20 x <4 x i32>]
  %output.curved.corrected.dem.f9.f8.f6.b_b = bitcast [20 x <4 x i32>]* %7 to i16*
  %8 = alloca [64 x <4 x i32>]
  %output.curved.corrected.dem.f9.f5 = bitcast [64 x <4 x i32>]* %8 to i16*
  %9 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f5.f4 = bitcast [32 x <4 x i32>]* %9 to i16*
  %10 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f5.f3 = bitcast [32 x <4 x i32>]* %10 to i16*
  %11 = alloca [64 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2 = bitcast [64 x <4 x i32>]* %11 to i16*
  %12 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f1 = bitcast [32 x <4 x i32>]* %12 to i16*
  %13 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f1.r_gr = bitcast [16 x <4 x i32>]* %13 to i16*
  %14 = alloca [32 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0 = bitcast [32 x <4 x i32>]* %14 to i16*
  %15 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_gb = bitcast [16 x <4 x i32>]* %15 to i16*
  %16 = alloca [16 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b = bitcast [16 x <4 x i32>]* %16 to i16*
  %17 = alloca [20 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r = bitcast [20 x <4 x i32>]* %17 to i16*
  %18 = alloca [20 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r = bitcast [20 x <4 x i32>]* %18 to i16*
  %19 = alloca [20 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b = bitcast [20 x <4 x i32>]* %19 to i16*
  %20 = alloca [23 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr = bitcast [23 x <4 x i32>]* %20 to i16*
  %21 = alloca [23 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb = bitcast [23 x <4 x i32>]* %21 to i16*
  %22 = alloca [90 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised = bitcast [90 x <4 x i32>]* %22 to i16*
  %23 = alloca [120 x <4 x i32>]
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in = bitcast [120 x <4 x i32>]* %23 to i16*
  %24 = add i32 %.result.dim.1, 15
  %25 = sdiv i32 %24, 16
  %extern_llvm.ptx.read.ctaid.y = call i32 @llvm.ptx.read.ctaid.y()
  %26 = icmp slt i32 %extern_llvm.ptx.read.ctaid.y, %25
  br i1 %26, label %output.blockidy_simt_loop, label %output.blockidy_simt_afterloop

output.blockidy_simt_loop:                        ; preds = %entry
  %27 = add i32 %.result.dim.0, 31
  %28 = sdiv i32 %27, 32
  %extern_llvm.ptx.read.ctaid.x = call i32 @llvm.ptx.read.ctaid.x()
  %29 = icmp slt i32 %extern_llvm.ptx.read.ctaid.x, %28
  br i1 %29, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop.preheader, label %output.blockidy_simt_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop.preheader: ; preds = %output.blockidy_simt_loop
  %30 = shl i32 %extern_llvm.ptx.read.ctaid.x, 5
  %memref_elem_ptr = bitcast i8* %.raw to i16*
;  store i32 %30, i32* %memref_elem_ptr
;  ret void
  %31 = shl i32 %extern_llvm.ptx.read.ctaid.y, 4
  %32 = add i32 %31, -4
  %33 = add i32 %.raw.dim.1, -1
  %34 = add i32 %.raw.dim.0, -1
  ;br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop.preheader

output.blockidy_simt_afterloop:                   ; preds = %output.c_loop.preheader, %output.threadidy_simt_loop, %output.curved.corrected.dem.f9.v0_afterloop, %output.blockidy_simt_loop, %entry
  ret void
  ;br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop.preheader

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop.preheader
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y = phi i32 [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop ], [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop.preheader ]
  %35 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y, 40
  %36 = add i32 %32, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y
  %37 = icmp slt i32 %36, %33
  %38 = select i1 %37, i32 %36, i32 %33
  %39 = icmp sgt i32 %38, 0
  %40 = select i1 %39, i32 %38, i32 0
  %41 = mul i32 %40, %.raw.dim.0
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop ]
  %42 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x, %30
  %43 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x, %35
  %memref = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %43
  %44 = add i32 %42, -4
  %45 = icmp slt i32 %44, %34
  %46 = select i1 %45, i32 %44, i32 %34
  %47 = icmp sgt i32 %46, 0
  %48 = select i1 %47, i32 %46, i32 0
  %49 = add i32 %48, %41
  %memref3 = getelementptr i16* %memref_elem_ptr, i32 %49
  %50 = load i16* %memref3
  store i16 %50, i16* %memref
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x, 1
  %51 = icmp eq i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar, 40
  br i1 %51, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y, 1
  ;%52 = icmp eq i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar, 24
  br i1 1, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop.preheader, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop
  ;br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop.preheader

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop.preheader: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop
  %offending = shl i32 %extern_llvm.ptx.read.ctaid.x, 4
  %out = bitcast i8* %.raw to i32*
  store i32 %offending, i32* %out
  ret void
}

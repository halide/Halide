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

define ptx_kernel void @kernel(i8* %.raw, i32 %.raw.dim.0, i32 %.raw.dim.1, i8* %.result, i32 %.result.dim.0, i32 %.result.dim.1, i32 %.result.dim.2, i8* %output.curved.corrected.matrix, i32 %output.curved.corrected.matrix.y.min, i8* %output.curved.curve, i32 %output.nilc) {
entry:
  %0 = alloca <4 x i32>, i32 256
  %output.curved.corrected.dem.f9 = bitcast <4 x i32>* %0 to i16*
  %1 = alloca <4 x i32>, i32 64
  %output.curved.corrected.dem.f9.f8 = bitcast <4 x i32>* %1 to i16*
  %2 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f8.f7 = bitcast <4 x i32>* %2 to i16*
  %3 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f8.f7.b_r = bitcast <4 x i32>* %3 to i16*
  %4 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f8.f7.b_gr = bitcast <4 x i32>* %4 to i16*
  %5 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f8.f6 = bitcast <4 x i32>* %5 to i16*
  %6 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f8.f6.b_gb = bitcast <4 x i32>* %6 to i16*
  %7 = alloca <4 x i32>, i32 20
  %output.curved.corrected.dem.f9.f8.f6.b_b = bitcast <4 x i32>* %7 to i16*
  %8 = alloca <4 x i32>, i32 64
  %output.curved.corrected.dem.f9.f5 = bitcast <4 x i32>* %8 to i16*
  %9 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f5.f4 = bitcast <4 x i32>* %9 to i16*
  %10 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f5.f3 = bitcast <4 x i32>* %10 to i16*
  %11 = alloca <4 x i32>, i32 64
  %output.curved.corrected.dem.f9.f2 = bitcast <4 x i32>* %11 to i16*
  %12 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f2.f1 = bitcast <4 x i32>* %12 to i16*
  %13 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f2.f1.r_gr = bitcast <4 x i32>* %13 to i16*
  %14 = alloca <4 x i32>, i32 32
  %output.curved.corrected.dem.f9.f2.f0 = bitcast <4 x i32>* %14 to i16*
  %15 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f2.f0.r_gb = bitcast <4 x i32>* %15 to i16*
  %16 = alloca <4 x i32>, i32 16
  %output.curved.corrected.dem.f9.f2.f0.r_b = bitcast <4 x i32>* %16 to i16*
  %17 = alloca <4 x i32>, i32 20
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r = bitcast <4 x i32>* %17 to i16*
  %18 = alloca <4 x i32>, i32 20
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r = bitcast <4 x i32>* %18 to i16*
  %19 = alloca <4 x i32>, i32 20
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b = bitcast <4 x i32>* %19 to i16*
  %20 = alloca <4 x i32>, i32 23
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr = bitcast <4 x i32>* %20 to i16*
  %21 = alloca <4 x i32>, i32 23
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb = bitcast <4 x i32>* %21 to i16*
  %22 = alloca <4 x i32>, i32 90
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised = bitcast <4 x i32>* %22 to i16*
  %23 = alloca <4 x i32>, i32 120
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in = bitcast <4 x i32>* %23 to i16*
  %24 = add i32 %.result.dim.1, 15
  %25 = sdiv i32 %24, 16
  %extern_llvm.ptx.read.ctaid.y = call i32 @llvm.ptx.read.ctaid.y()
  %26 = icmp slt i32 %extern_llvm.ptx.read.ctaid.y, %25
  br i1 %26, label %output.blockidy_simt_loop, label %output.blockidy_simt_afterloop

output.blockidy_simt_loop:                        ; preds = %entry
  %extern_llvm.ptx.read.ctaid.y1 = call i32 @llvm.ptx.read.ctaid.y()
  %output.blockidy = add i32 %extern_llvm.ptx.read.ctaid.y1, 0
  %27 = add i32 %.result.dim.0, 31
  %28 = sdiv i32 %27, 32
  %extern_llvm.ptx.read.ctaid.x = call i32 @llvm.ptx.read.ctaid.x()
  %29 = icmp slt i32 %extern_llvm.ptx.read.ctaid.x, %28
  br i1 %29, label %output.blockidx_simt_loop, label %output.blockidx_simt_afterloop

output.blockidy_simt_afterloop:                   ; preds = %output.blockidx_simt_afterloop, %entry
  ret void

output.blockidx_simt_loop:                        ; preds = %output.blockidy_simt_loop
  %extern_llvm.ptx.read.ctaid.x2 = call i32 @llvm.ptx.read.ctaid.x()
  %output.blockidx = add i32 %extern_llvm.ptx.read.ctaid.x2, 0
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop

output.blockidx_simt_afterloop:                   ; preds = %output.threadidy_simt_afterloop, %output.blockidy_simt_loop
  br label %output.blockidy_simt_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop, %output.blockidx_simt_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y = phi i32 [ 0, %output.blockidx_simt_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop ]
  %30 = mul i32 %output.blockidx, 16
  %31 = mul i32 %30, 2
  %32 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y, 40
  %33 = mul i32 %output.blockidx, 16
  %34 = mul i32 %33, 2
  %35 = add i32 %34, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x
  %36 = add i32 %35, %32
  %37 = sub i32 %36, %31
  %memref = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %37
  %memref_elem_ptr = bitcast i8* %.raw to i16*
  %38 = mul i32 %output.blockidy, 8
  %39 = mul i32 %38, 2
  %40 = add i32 %39, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y
  %41 = add i32 %40, -4
  %42 = add i32 %.raw.dim.1, -1
  %43 = icmp slt i32 %41, %42
  %44 = select i1 %43, i32 %41, i32 %42
  %45 = icmp sgt i32 %44, 0
  %46 = select i1 %45, i32 %44, i32 0
  %47 = mul i32 %.raw.dim.0, %46
  %48 = mul i32 %output.blockidx, 16
  %49 = mul i32 %48, 2
  %50 = add i32 %49, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x
  %51 = add i32 %50, -4
  %52 = add i32 %.raw.dim.0, -1
  %53 = icmp slt i32 %51, %52
  %54 = select i1 %53, i32 %51, i32 %52
  %55 = icmp sgt i32 %54, 0
  %56 = select i1 %55, i32 %54, i32 0
  %57 = add i32 %56, %47
  %memref3 = getelementptr i16* %memref_elem_ptr, i32 %57
  %58 = load i16* %memref3
  store i16 %58, i16* %memref
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x, 1
  %59 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_nextvar, 40
  br i1 %59, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y, 1
  %60 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_nextvar, 24
  br i1 %60, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop ]
  %61 = mul i32 %output.blockidx, 16
  %62 = mul i32 %61, 2
  %63 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 36
  %64 = mul i32 %output.blockidx, 16
  %65 = mul i32 %64, 2
  %66 = add i32 %65, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %67 = add i32 %66, %63
  %68 = sub i32 %67, %62
  %memref4 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised, i32 %68
  %69 = mul i32 %output.blockidx, 16
  %70 = mul i32 %69, 2
  %71 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %72 = mul i32 %output.blockidx, 16
  %73 = mul i32 %72, 2
  %74 = add i32 %73, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %75 = add i32 %74, %71
  %76 = sub i32 %75, %70
  %77 = add i32 %76, 82
  %memref5 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %77
  %78 = load i16* %memref5
  %79 = mul i32 %output.blockidx, 16
  %80 = mul i32 %79, 2
  %81 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %82 = mul i32 %output.blockidx, 16
  %83 = mul i32 %82, 2
  %84 = add i32 %83, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %85 = add i32 %84, %81
  %86 = sub i32 %85, %80
  %87 = add i32 %86, 80
  %memref6 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %87
  %88 = load i16* %memref6
  %89 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %90 = add i32 %89, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %91 = add i32 %90, 84
  %memref7 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %91
  %92 = load i16* %memref7
  %93 = icmp sgt i16 %88, %92
  %94 = select i1 %93, i16 %88, i16 %92
  %95 = mul i32 %output.blockidx, 16
  %96 = mul i32 %95, 2
  %97 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %98 = mul i32 %output.blockidx, 16
  %99 = mul i32 %98, 2
  %100 = add i32 %99, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %101 = add i32 %100, %97
  %102 = sub i32 %101, %96
  %103 = add i32 %102, 2
  %memref8 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %103
  %104 = load i16* %memref8
  %105 = mul i32 %output.blockidx, 16
  %106 = mul i32 %105, 2
  %107 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %108 = mul i32 %output.blockidx, 16
  %109 = mul i32 %108, 2
  %110 = add i32 %109, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %111 = add i32 %110, %107
  %112 = sub i32 %111, %106
  %113 = add i32 %112, 162
  %memref9 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %113
  %114 = load i16* %memref9
  %115 = icmp sgt i16 %104, %114
  %116 = select i1 %115, i16 %104, i16 %114
  %117 = icmp sgt i16 %94, %116
  %118 = select i1 %117, i16 %94, i16 %116
  %119 = icmp slt i16 %78, %118
  %120 = select i1 %119, i16 %78, i16 %118
  %121 = mul i32 %output.blockidx, 16
  %122 = mul i32 %121, 2
  %123 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %124 = mul i32 %output.blockidx, 16
  %125 = mul i32 %124, 2
  %126 = add i32 %125, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %127 = add i32 %126, %123
  %128 = sub i32 %127, %122
  %129 = add i32 %128, 80
  %memref10 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %129
  %130 = load i16* %memref10
  %131 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %132 = add i32 %131, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %133 = add i32 %132, 84
  %memref11 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %133
  %134 = load i16* %memref11
  %135 = icmp slt i16 %130, %134
  %136 = select i1 %135, i16 %130, i16 %134
  %137 = mul i32 %output.blockidx, 16
  %138 = mul i32 %137, 2
  %139 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %140 = mul i32 %output.blockidx, 16
  %141 = mul i32 %140, 2
  %142 = add i32 %141, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %143 = add i32 %142, %139
  %144 = sub i32 %143, %138
  %145 = add i32 %144, 2
  %memref12 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %145
  %146 = load i16* %memref12
  %147 = mul i32 %output.blockidx, 16
  %148 = mul i32 %147, 2
  %149 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 40
  %150 = mul i32 %output.blockidx, 16
  %151 = mul i32 %150, 2
  %152 = add i32 %151, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x
  %153 = add i32 %152, %149
  %154 = sub i32 %153, %148
  %155 = add i32 %154, 162
  %memref13 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.in, i32 %155
  %156 = load i16* %memref13
  %157 = icmp slt i16 %146, %156
  %158 = select i1 %157, i16 %146, i16 %156
  %159 = icmp slt i16 %136, %158
  %160 = select i1 %159, i16 %136, i16 %158
  %161 = icmp sgt i16 %120, %160
  %162 = select i1 %161, i16 %120, i16 %160
  store i16 %162, i16* %memref4
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x, 1
  %163 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_nextvar, 36
  br i1 %163, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y, 1
  %164 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_nextvar, 20
  br i1 %164, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop ]
  %165 = mul i32 %output.blockidx, 16
  %166 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y, 18
  %167 = mul i32 %output.blockidx, 16
  %168 = add i32 %167, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x
  %169 = add i32 %168, %166
  %170 = sub i32 %169, %165
  %memref14 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %170
  %171 = mul i32 %output.blockidx, 16
  %172 = mul i32 %171, 2
  %173 = mul i32 %output.blockidy, 8
  %174 = mul i32 %173, 2
  %175 = mul i32 %output.blockidy, 8
  %176 = add i32 %175, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y
  %177 = mul i32 %176, 2
  %178 = sub i32 %177, %174
  %179 = mul i32 %178, 36
  %180 = mul i32 %output.blockidx, 16
  %181 = add i32 %180, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x
  %182 = mul i32 %181, 2
  %183 = add i32 %182, %179
  %184 = sub i32 %183, %172
  %185 = add i32 %184, 37
  %memref15 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised, i32 %185
  %186 = load i16* %memref15
  store i16 %186, i16* %memref14
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x, 1
  %187 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_nextvar, 18
  br i1 %187, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y, 1
  %188 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_nextvar, 10
  br i1 %188, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop ]
  %189 = mul i32 %output.blockidx, 16
  %190 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y, 18
  %191 = mul i32 %output.blockidx, 16
  %192 = add i32 %191, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x
  %193 = add i32 %192, %190
  %194 = sub i32 %193, %189
  %memref16 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %194
  %195 = mul i32 %output.blockidx, 16
  %196 = mul i32 %195, 2
  %197 = mul i32 %output.blockidy, 8
  %198 = mul i32 %197, 2
  %199 = mul i32 %output.blockidy, 8
  %200 = add i32 %199, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y
  %201 = mul i32 %200, 2
  %202 = sub i32 %201, %198
  %203 = mul i32 %202, 36
  %204 = mul i32 %output.blockidx, 16
  %205 = add i32 %204, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x
  %206 = mul i32 %205, 2
  %207 = add i32 %206, %203
  %208 = sub i32 %207, %196
  %memref17 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised, i32 %208
  %209 = load i16* %memref17
  store i16 %209, i16* %memref16
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x, 1
  %210 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_nextvar, 18
  br i1 %210, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y, 1
  %211 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_nextvar, 10
  br i1 %211, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop ]
  %212 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 17
  %213 = add i32 %212, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %memref18 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %213
  %214 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %215 = add i32 %214, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %216 = add i32 %215, 1
  %memref19 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %216
  %217 = load i16* %memref19
  %218 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %219 = add i32 %218, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %220 = add i32 %219, 19
  %memref20 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %220
  %221 = load i16* %memref20
  %222 = add i16 %221, %217
  %223 = sdiv i16 %222, 2
  %224 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %225 = add i32 %224, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %226 = add i32 %225, 1
  %memref21 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %226
  %227 = load i16* %memref21
  %228 = mul i32 %output.blockidx, 16
  %229 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %230 = mul i32 %output.blockidx, 16
  %231 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x, %230
  %232 = add i32 %231, %229
  %233 = sub i32 %232, %228
  %memref22 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %233
  %234 = load i16* %memref22
  %235 = add i16 %234, %227
  %236 = sdiv i16 %235, 2
  %237 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %238 = add i32 %237, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %239 = add i32 %238, 1
  %memref23 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %239
  %240 = load i16* %memref23
  %241 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %242 = add i32 %241, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %243 = add i32 %242, 19
  %memref24 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %243
  %244 = load i16* %memref24
  %245 = sub i16 %244, %240
  %246 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %247 = add i32 %246, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %248 = add i32 %247, 1
  %memref25 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %248
  %249 = load i16* %memref25
  %250 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %251 = add i32 %250, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %252 = add i32 %251, 19
  %memref26 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %252
  %253 = load i16* %memref26
  %254 = sub i16 %253, %249
  %255 = sub i16 0, %254
  %256 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %257 = add i32 %256, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %258 = add i32 %257, 1
  %memref27 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %258
  %259 = load i16* %memref27
  %260 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %261 = add i32 %260, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %262 = add i32 %261, 19
  %memref28 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %262
  %263 = load i16* %memref28
  %264 = sub i16 %263, %259
  %265 = icmp slt i16 %264, 0
  %266 = select i1 %265, i16 %255, i16 %245
  %267 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %268 = add i32 %267, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %269 = add i32 %268, 1
  %memref29 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %269
  %270 = load i16* %memref29
  %271 = mul i32 %output.blockidx, 16
  %272 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %273 = mul i32 %output.blockidx, 16
  %274 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x, %273
  %275 = add i32 %274, %272
  %276 = sub i32 %275, %271
  %memref30 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %276
  %277 = load i16* %memref30
  %278 = sub i16 %277, %270
  %279 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %280 = add i32 %279, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %281 = add i32 %280, 1
  %memref31 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %281
  %282 = load i16* %memref31
  %283 = mul i32 %output.blockidx, 16
  %284 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %285 = mul i32 %output.blockidx, 16
  %286 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x, %285
  %287 = add i32 %286, %284
  %288 = sub i32 %287, %283
  %memref32 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %288
  %289 = load i16* %memref32
  %290 = sub i16 %289, %282
  %291 = sub i16 0, %290
  %292 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %293 = add i32 %292, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x
  %294 = add i32 %293, 1
  %memref33 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %294
  %295 = load i16* %memref33
  %296 = mul i32 %output.blockidx, 16
  %297 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 18
  %298 = mul i32 %output.blockidx, 16
  %299 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x, %298
  %300 = add i32 %299, %297
  %301 = sub i32 %300, %296
  %memref34 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %301
  %302 = load i16* %memref34
  %303 = sub i16 %302, %295
  %304 = icmp slt i16 %303, 0
  %305 = select i1 %304, i16 %291, i16 %278
  %306 = icmp slt i16 %305, %266
  %307 = select i1 %306, i16 %236, i16 %223
  store i16 %307, i16* %memref18
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x, 1
  %308 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_nextvar, 17
  br i1 %308, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y, 1
  %309 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_nextvar, 9
  br i1 %309, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop ]
  %310 = mul i32 %output.blockidx, 16
  %311 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 17
  %312 = mul i32 %output.blockidx, 16
  %313 = add i32 %312, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %314 = add i32 %313, %311
  %315 = sub i32 %314, %310
  %memref35 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %315
  %316 = mul i32 %output.blockidx, 16
  %317 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %318 = mul i32 %output.blockidx, 16
  %319 = add i32 %318, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %320 = add i32 %319, %317
  %321 = sub i32 %320, %316
  %322 = add i32 %321, 18
  %memref36 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %322
  %323 = load i16* %memref36
  %324 = mul i32 %output.blockidx, 16
  %325 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %326 = mul i32 %output.blockidx, 16
  %327 = add i32 %326, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %328 = add i32 %327, %325
  %329 = sub i32 %328, %324
  %memref37 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %329
  %330 = load i16* %memref37
  %331 = add i16 %330, %323
  %332 = sdiv i16 %331, 2
  %333 = mul i32 %output.blockidx, 16
  %334 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %335 = mul i32 %output.blockidx, 16
  %336 = add i32 %335, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %337 = add i32 %336, %334
  %338 = sub i32 %337, %333
  %339 = add i32 %338, 18
  %memref38 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %339
  %340 = load i16* %memref38
  %341 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %342 = add i32 %341, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %343 = add i32 %342, 19
  %memref39 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %343
  %344 = load i16* %memref39
  %345 = add i16 %344, %340
  %346 = sdiv i16 %345, 2
  %347 = mul i32 %output.blockidx, 16
  %348 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %349 = mul i32 %output.blockidx, 16
  %350 = add i32 %349, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %351 = add i32 %350, %348
  %352 = sub i32 %351, %347
  %353 = add i32 %352, 18
  %memref40 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %353
  %354 = load i16* %memref40
  %355 = mul i32 %output.blockidx, 16
  %356 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %357 = mul i32 %output.blockidx, 16
  %358 = add i32 %357, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %359 = add i32 %358, %356
  %360 = sub i32 %359, %355
  %memref41 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %360
  %361 = load i16* %memref41
  %362 = sub i16 %361, %354
  %363 = mul i32 %output.blockidx, 16
  %364 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %365 = mul i32 %output.blockidx, 16
  %366 = add i32 %365, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %367 = add i32 %366, %364
  %368 = sub i32 %367, %363
  %369 = add i32 %368, 18
  %memref42 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %369
  %370 = load i16* %memref42
  %371 = mul i32 %output.blockidx, 16
  %372 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %373 = mul i32 %output.blockidx, 16
  %374 = add i32 %373, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %375 = add i32 %374, %372
  %376 = sub i32 %375, %371
  %memref43 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %376
  %377 = load i16* %memref43
  %378 = sub i16 %377, %370
  %379 = sub i16 0, %378
  %380 = mul i32 %output.blockidx, 16
  %381 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %382 = mul i32 %output.blockidx, 16
  %383 = add i32 %382, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %384 = add i32 %383, %381
  %385 = sub i32 %384, %380
  %386 = add i32 %385, 18
  %memref44 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %386
  %387 = load i16* %memref44
  %388 = mul i32 %output.blockidx, 16
  %389 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %390 = mul i32 %output.blockidx, 16
  %391 = add i32 %390, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %392 = add i32 %391, %389
  %393 = sub i32 %392, %388
  %memref45 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %393
  %394 = load i16* %memref45
  %395 = sub i16 %394, %387
  %396 = icmp slt i16 %395, 0
  %397 = select i1 %396, i16 %379, i16 %362
  %398 = mul i32 %output.blockidx, 16
  %399 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %400 = mul i32 %output.blockidx, 16
  %401 = add i32 %400, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %402 = add i32 %401, %399
  %403 = sub i32 %402, %398
  %404 = add i32 %403, 18
  %memref46 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %404
  %405 = load i16* %memref46
  %406 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %407 = add i32 %406, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %408 = add i32 %407, 19
  %memref47 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %408
  %409 = load i16* %memref47
  %410 = sub i16 %409, %405
  %411 = mul i32 %output.blockidx, 16
  %412 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %413 = mul i32 %output.blockidx, 16
  %414 = add i32 %413, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %415 = add i32 %414, %412
  %416 = sub i32 %415, %411
  %417 = add i32 %416, 18
  %memref48 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %417
  %418 = load i16* %memref48
  %419 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %420 = add i32 %419, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %421 = add i32 %420, 19
  %memref49 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %421
  %422 = load i16* %memref49
  %423 = sub i16 %422, %418
  %424 = sub i16 0, %423
  %425 = mul i32 %output.blockidx, 16
  %426 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %427 = mul i32 %output.blockidx, 16
  %428 = add i32 %427, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %429 = add i32 %428, %426
  %430 = sub i32 %429, %425
  %431 = add i32 %430, 18
  %memref50 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %431
  %432 = load i16* %memref50
  %433 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 18
  %434 = add i32 %433, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x
  %435 = add i32 %434, 19
  %memref51 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %435
  %436 = load i16* %memref51
  %437 = sub i16 %436, %432
  %438 = icmp slt i16 %437, 0
  %439 = select i1 %438, i16 %424, i16 %410
  %440 = icmp slt i16 %439, %397
  %441 = select i1 %440, i16 %346, i16 %332
  store i16 %441, i16* %memref35
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x, 1
  %442 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_nextvar, 17
  br i1 %442, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y, 1
  %443 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_nextvar, 9
  br i1 %443, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.g_r.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop ]
  %444 = mul i32 %output.blockidx, 16
  %445 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y, 17
  %446 = mul i32 %output.blockidx, 16
  %447 = add i32 %446, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x
  %448 = add i32 %447, %445
  %449 = sub i32 %448, %444
  %memref52 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %449
  %450 = mul i32 %output.blockidx, 16
  %451 = mul i32 %450, 2
  %452 = mul i32 %output.blockidy, 8
  %453 = mul i32 %452, 2
  %454 = mul i32 %output.blockidy, 8
  %455 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y, %454
  %456 = mul i32 %455, 2
  %457 = sub i32 %456, %453
  %458 = mul i32 %457, 36
  %459 = mul i32 %output.blockidx, 16
  %460 = add i32 %459, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x
  %461 = mul i32 %460, 2
  %462 = add i32 %461, %458
  %463 = sub i32 %462, %451
  %464 = add i32 %463, 73
  %memref53 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised, i32 %464
  %465 = load i16* %memref53
  store i16 %465, i16* %memref52
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x, 1
  %466 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_nextvar, 17
  br i1 %466, label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y, 1
  %467 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_nextvar, 9
  br i1 %467, label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.y_loop

output.curved.corrected.dem.f9.f2.f0.r_b.y_loop:  ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_b.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.r_r.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_b.x_loop

output.curved.corrected.dem.f9.f2.f0.r_b.x_loop:  ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_b.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_b.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_b.x_loop ]
  %468 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 16
  %469 = add i32 %468, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %memref54 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b, i32 %469
  %470 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %471 = add i32 %470, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %472 = add i32 %471, 18
  %memref55 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %472
  %473 = load i16* %memref55
  %474 = mul i32 %output.blockidx, 16
  %475 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %476 = mul i32 %output.blockidx, 16
  %477 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %476
  %478 = add i32 %477, %475
  %479 = sub i32 %478, %474
  %memref56 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %479
  %480 = load i16* %memref56
  %481 = add i16 %480, %473
  %482 = sdiv i16 %481, 2
  %483 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %484 = add i32 %483, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %485 = add i32 %484, 18
  %memref57 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %485
  %486 = load i16* %memref57
  %487 = mul i32 %output.blockidx, 16
  %488 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %489 = mul i32 %output.blockidx, 16
  %490 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %489
  %491 = add i32 %490, %488
  %492 = sub i32 %491, %487
  %memref58 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %492
  %493 = load i16* %memref58
  %494 = add i16 %493, %486
  %495 = sdiv i16 %494, 2
  %496 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %497 = add i32 %496, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %498 = add i32 %497, 17
  %memref59 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %498
  %499 = load i16* %memref59
  %500 = sub i16 %499, %495
  %501 = add i16 %500, %482
  %502 = mul i32 %output.blockidx, 16
  %503 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %504 = mul i32 %output.blockidx, 16
  %505 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %504
  %506 = add i32 %505, %503
  %507 = sub i32 %506, %502
  %508 = add i32 %507, 17
  %memref60 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %508
  %509 = load i16* %memref60
  %510 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %511 = add i32 %510, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %512 = add i32 %511, 1
  %memref61 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %512
  %513 = load i16* %memref61
  %514 = add i16 %513, %509
  %515 = sdiv i16 %514, 2
  %516 = mul i32 %output.blockidx, 16
  %517 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %518 = mul i32 %output.blockidx, 16
  %519 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %518
  %520 = add i32 %519, %517
  %521 = sub i32 %520, %516
  %522 = add i32 %521, 17
  %memref62 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %522
  %523 = load i16* %memref62
  %524 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %525 = add i32 %524, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %526 = add i32 %525, 1
  %memref63 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %526
  %527 = load i16* %memref63
  %528 = add i16 %527, %523
  %529 = sdiv i16 %528, 2
  %530 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %531 = add i32 %530, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %532 = add i32 %531, 17
  %memref64 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %532
  %533 = load i16* %memref64
  %534 = sub i16 %533, %529
  %535 = add i16 %534, %515
  %536 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %537 = add i32 %536, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %538 = add i32 %537, 18
  %memref65 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %538
  %539 = load i16* %memref65
  %540 = mul i32 %output.blockidx, 16
  %541 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %542 = mul i32 %output.blockidx, 16
  %543 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %542
  %544 = add i32 %543, %541
  %545 = sub i32 %544, %540
  %memref66 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %545
  %546 = load i16* %memref66
  %547 = sub i16 %546, %539
  %548 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %549 = add i32 %548, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %550 = add i32 %549, 18
  %memref67 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %550
  %551 = load i16* %memref67
  %552 = mul i32 %output.blockidx, 16
  %553 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %554 = mul i32 %output.blockidx, 16
  %555 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %554
  %556 = add i32 %555, %553
  %557 = sub i32 %556, %552
  %memref68 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %557
  %558 = load i16* %memref68
  %559 = sub i16 %558, %551
  %560 = sub i16 0, %559
  %561 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %562 = add i32 %561, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %563 = add i32 %562, 18
  %memref69 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %563
  %564 = load i16* %memref69
  %565 = mul i32 %output.blockidx, 16
  %566 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %567 = mul i32 %output.blockidx, 16
  %568 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %567
  %569 = add i32 %568, %566
  %570 = sub i32 %569, %565
  %memref70 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %570
  %571 = load i16* %memref70
  %572 = sub i16 %571, %564
  %573 = icmp slt i16 %572, 0
  %574 = select i1 %573, i16 %560, i16 %547
  %575 = mul i32 %output.blockidx, 16
  %576 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %577 = mul i32 %output.blockidx, 16
  %578 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %577
  %579 = add i32 %578, %576
  %580 = sub i32 %579, %575
  %581 = add i32 %580, 17
  %memref71 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %581
  %582 = load i16* %memref71
  %583 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %584 = add i32 %583, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %585 = add i32 %584, 1
  %memref72 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %585
  %586 = load i16* %memref72
  %587 = sub i16 %586, %582
  %588 = mul i32 %output.blockidx, 16
  %589 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %590 = mul i32 %output.blockidx, 16
  %591 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %590
  %592 = add i32 %591, %589
  %593 = sub i32 %592, %588
  %594 = add i32 %593, 17
  %memref73 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %594
  %595 = load i16* %memref73
  %596 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %597 = add i32 %596, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %598 = add i32 %597, 1
  %memref74 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %598
  %599 = load i16* %memref74
  %600 = sub i16 %599, %595
  %601 = sub i16 0, %600
  %602 = mul i32 %output.blockidx, 16
  %603 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %604 = mul i32 %output.blockidx, 16
  %605 = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, %604
  %606 = add i32 %605, %603
  %607 = sub i32 %606, %602
  %608 = add i32 %607, 17
  %memref75 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %608
  %609 = load i16* %memref75
  %610 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 17
  %611 = add i32 %610, %output.curved.corrected.dem.f9.f2.f0.r_b.x
  %612 = add i32 %611, 1
  %memref76 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %612
  %613 = load i16* %memref76
  %614 = sub i16 %613, %609
  %615 = icmp slt i16 %614, 0
  %616 = select i1 %615, i16 %601, i16 %587
  %617 = icmp slt i16 %616, %574
  %618 = select i1 %617, i16 %535, i16 %501
  store i16 %618, i16* %memref54
  %output.curved.corrected.dem.f9.f2.f0.r_b.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x, 1
  %619 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.x_nextvar, 16
  br i1 %619, label %output.curved.corrected.dem.f9.f2.f0.r_b.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_b.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y, 1
  %620 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_b.y_nextvar, 8
  br i1 %620, label %output.curved.corrected.dem.f9.f2.f0.r_b.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_b.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_b.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_b.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.r_gb.y_loop

output.curved.corrected.dem.f9.f2.f0.r_gb.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_gb.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_b.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.r_gb.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_b.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.r_gb.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_gb.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop

output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop, %output.curved.corrected.dem.f9.f2.f0.r_gb.y_loop
  %output.curved.corrected.dem.f9.f2.f0.r_gb.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_gb.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.r_gb.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop ]
  %621 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 16
  %622 = add i32 %621, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %memref77 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_gb, i32 %622
  %623 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 17
  %624 = add i32 %623, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %625 = add i32 %624, 18
  %memref78 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %625
  %626 = load i16* %memref78
  %627 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 17
  %628 = add i32 %627, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %629 = add i32 %628, 1
  %memref79 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %629
  %630 = load i16* %memref79
  %631 = add i16 %630, %626
  %632 = sdiv i16 %631, 2
  %633 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 17
  %634 = add i32 %633, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %635 = add i32 %634, 18
  %memref80 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %635
  %636 = load i16* %memref80
  %637 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 17
  %638 = add i32 %637, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %639 = add i32 %638, 1
  %memref81 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %639
  %640 = load i16* %memref81
  %641 = add i16 %640, %636
  %642 = sdiv i16 %641, 2
  %643 = mul i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 18
  %644 = add i32 %643, %output.curved.corrected.dem.f9.f2.f0.r_gb.x
  %645 = add i32 %644, 19
  %memref82 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %645
  %646 = load i16* %memref82
  %647 = sub i16 %646, %642
  %648 = add i16 %647, %632
  store i16 %648, i16* %memref77
  %output.curved.corrected.dem.f9.f2.f0.r_gb.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.x, 1
  %649 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.x_nextvar, 16
  br i1 %649, label %output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop, label %output.curved.corrected.dem.f9.f2.f0.r_gb.x_afterloop

output.curved.corrected.dem.f9.f2.f0.r_gb.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_gb.x_loop
  %output.curved.corrected.dem.f9.f2.f0.r_gb.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y, 1
  %650 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.r_gb.y_nextvar, 8
  br i1 %650, label %output.curved.corrected.dem.f9.f2.f0.r_gb.y_loop, label %output.curved.corrected.dem.f9.f2.f0.r_gb.y_afterloop

output.curved.corrected.dem.f9.f2.f0.r_gb.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.r_gb.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f0.y_loop

output.curved.corrected.dem.f9.f2.f0.y_loop:      ; preds = %output.curved.corrected.dem.f9.f2.f0.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.r_gb.y_afterloop
  %output.curved.corrected.dem.f9.f2.f0.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.r_gb.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f0.y_nextvar, %output.curved.corrected.dem.f9.f2.f0.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f0.x_loop

output.curved.corrected.dem.f9.f2.f0.x_loop:      ; preds = %output.curved.corrected.dem.f9.f2.f0.x_loop, %output.curved.corrected.dem.f9.f2.f0.y_loop
  %output.curved.corrected.dem.f9.f2.f0.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.y_loop ], [ %output.curved.corrected.dem.f9.f2.f0.x_nextvar, %output.curved.corrected.dem.f9.f2.f0.x_loop ]
  %651 = mul i32 %output.curved.corrected.dem.f9.f2.f0.y, 32
  %652 = add i32 %651, %output.curved.corrected.dem.f9.f2.f0.x
  %memref83 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0, i32 %652
  %653 = sdiv i32 %output.curved.corrected.dem.f9.f2.f0.x, 2
  %654 = mul i32 %output.curved.corrected.dem.f9.f2.f0.y, 16
  %655 = add i32 %654, %653
  %memref84 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_gb, i32 %655
  %656 = load i16* %memref84
  %657 = sdiv i32 %output.curved.corrected.dem.f9.f2.f0.x, 2
  %658 = mul i32 %output.curved.corrected.dem.f9.f2.f0.y, 16
  %659 = add i32 %658, %657
  %memref85 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b, i32 %659
  %660 = load i16* %memref85
  %661 = srem i32 %output.curved.corrected.dem.f9.f2.f0.x, 2
  %662 = add i32 %661, 2
  %663 = srem i32 %662, 2
  %664 = icmp eq i32 %663, 0
  %665 = select i1 %664, i16 %660, i16 %656
  store i16 %665, i16* %memref83
  %output.curved.corrected.dem.f9.f2.f0.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.x, 1
  %666 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.x_nextvar, 32
  br i1 %666, label %output.curved.corrected.dem.f9.f2.f0.x_loop, label %output.curved.corrected.dem.f9.f2.f0.x_afterloop

output.curved.corrected.dem.f9.f2.f0.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.x_loop
  %output.curved.corrected.dem.f9.f2.f0.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f0.y, 1
  %667 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f0.y_nextvar, 8
  br i1 %667, label %output.curved.corrected.dem.f9.f2.f0.y_loop, label %output.curved.corrected.dem.f9.f2.f0.y_afterloop

output.curved.corrected.dem.f9.f2.f0.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f0.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f1.r_gr.y_loop

output.curved.corrected.dem.f9.f2.f1.r_gr.y_loop: ; preds = %output.curved.corrected.dem.f9.f2.f1.r_gr.x_afterloop, %output.curved.corrected.dem.f9.f2.f0.y_afterloop
  %output.curved.corrected.dem.f9.f2.f1.r_gr.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f0.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f1.r_gr.y_nextvar, %output.curved.corrected.dem.f9.f2.f1.r_gr.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop

output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop: ; preds = %output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop, %output.curved.corrected.dem.f9.f2.f1.r_gr.y_loop
  %output.curved.corrected.dem.f9.f2.f1.r_gr.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f1.r_gr.y_loop ], [ %output.curved.corrected.dem.f9.f2.f1.r_gr.x_nextvar, %output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop ]
  %668 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 16
  %669 = add i32 %668, %output.curved.corrected.dem.f9.f2.f1.r_gr.x
  %memref86 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f1.r_gr, i32 %669
  %670 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 17
  %671 = add i32 %670, %output.curved.corrected.dem.f9.f2.f1.r_gr.x
  %672 = add i32 %671, 1
  %memref87 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %672
  %673 = load i16* %memref87
  %674 = mul i32 %output.blockidx, 16
  %675 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 17
  %676 = mul i32 %output.blockidx, 16
  %677 = add i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.x, %676
  %678 = add i32 %677, %675
  %679 = sub i32 %678, %674
  %memref88 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %679
  %680 = load i16* %memref88
  %681 = add i16 %680, %673
  %682 = sdiv i16 %681, 2
  %683 = mul i32 %output.blockidx, 16
  %684 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 17
  %685 = mul i32 %output.blockidx, 16
  %686 = add i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.x, %685
  %687 = add i32 %686, %684
  %688 = sub i32 %687, %683
  %memref89 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %688
  %689 = load i16* %memref89
  %690 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 17
  %691 = add i32 %690, %output.curved.corrected.dem.f9.f2.f1.r_gr.x
  %692 = add i32 %691, 1
  %memref90 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %692
  %693 = load i16* %memref90
  %694 = add i16 %693, %689
  %695 = sdiv i16 %694, 2
  %696 = mul i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 18
  %697 = add i32 %696, %output.curved.corrected.dem.f9.f2.f1.r_gr.x
  %698 = add i32 %697, 19
  %memref91 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %698
  %699 = load i16* %memref91
  %700 = sub i16 %699, %695
  %701 = add i16 %700, %682
  store i16 %701, i16* %memref86
  %output.curved.corrected.dem.f9.f2.f1.r_gr.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.x, 1
  %702 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.x_nextvar, 16
  br i1 %702, label %output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop, label %output.curved.corrected.dem.f9.f2.f1.r_gr.x_afterloop

output.curved.corrected.dem.f9.f2.f1.r_gr.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f1.r_gr.x_loop
  %output.curved.corrected.dem.f9.f2.f1.r_gr.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y, 1
  %703 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f1.r_gr.y_nextvar, 8
  br i1 %703, label %output.curved.corrected.dem.f9.f2.f1.r_gr.y_loop, label %output.curved.corrected.dem.f9.f2.f1.r_gr.y_afterloop

output.curved.corrected.dem.f9.f2.f1.r_gr.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f1.r_gr.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.f1.y_loop

output.curved.corrected.dem.f9.f2.f1.y_loop:      ; preds = %output.curved.corrected.dem.f9.f2.f1.x_afterloop, %output.curved.corrected.dem.f9.f2.f1.r_gr.y_afterloop
  %output.curved.corrected.dem.f9.f2.f1.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f1.r_gr.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.f1.y_nextvar, %output.curved.corrected.dem.f9.f2.f1.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.f1.x_loop

output.curved.corrected.dem.f9.f2.f1.x_loop:      ; preds = %output.curved.corrected.dem.f9.f2.f1.x_loop, %output.curved.corrected.dem.f9.f2.f1.y_loop
  %output.curved.corrected.dem.f9.f2.f1.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f1.y_loop ], [ %output.curved.corrected.dem.f9.f2.f1.x_nextvar, %output.curved.corrected.dem.f9.f2.f1.x_loop ]
  %704 = mul i32 %output.curved.corrected.dem.f9.f2.f1.y, 32
  %705 = add i32 %704, %output.curved.corrected.dem.f9.f2.f1.x
  %memref92 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f1, i32 %705
  %706 = sdiv i32 %output.curved.corrected.dem.f9.f2.f1.x, 2
  %707 = mul i32 %output.curved.corrected.dem.f9.f2.f1.y, 17
  %708 = add i32 %707, %706
  %709 = add i32 %708, 1
  %memref93 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.r_r, i32 %709
  %710 = load i16* %memref93
  %711 = sdiv i32 %output.curved.corrected.dem.f9.f2.f1.x, 2
  %712 = mul i32 %output.curved.corrected.dem.f9.f2.f1.y, 16
  %713 = add i32 %712, %711
  %memref94 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f1.r_gr, i32 %713
  %714 = load i16* %memref94
  %715 = srem i32 %output.curved.corrected.dem.f9.f2.f1.x, 2
  %716 = add i32 %715, 2
  %717 = srem i32 %716, 2
  %718 = icmp eq i32 %717, 0
  %719 = select i1 %718, i16 %714, i16 %710
  store i16 %719, i16* %memref92
  %output.curved.corrected.dem.f9.f2.f1.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f1.x, 1
  %720 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f1.x_nextvar, 32
  br i1 %720, label %output.curved.corrected.dem.f9.f2.f1.x_loop, label %output.curved.corrected.dem.f9.f2.f1.x_afterloop

output.curved.corrected.dem.f9.f2.f1.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f1.x_loop
  %output.curved.corrected.dem.f9.f2.f1.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.f1.y, 1
  %721 = icmp ne i32 %output.curved.corrected.dem.f9.f2.f1.y_nextvar, 8
  br i1 %721, label %output.curved.corrected.dem.f9.f2.f1.y_loop, label %output.curved.corrected.dem.f9.f2.f1.y_afterloop

output.curved.corrected.dem.f9.f2.f1.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f2.f1.x_afterloop
  br label %output.curved.corrected.dem.f9.f2.y_loop

output.curved.corrected.dem.f9.f2.y_loop:         ; preds = %output.curved.corrected.dem.f9.f2.x_afterloop, %output.curved.corrected.dem.f9.f2.f1.y_afterloop
  %output.curved.corrected.dem.f9.f2.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.f1.y_afterloop ], [ %output.curved.corrected.dem.f9.f2.y_nextvar, %output.curved.corrected.dem.f9.f2.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f2.x_loop

output.curved.corrected.dem.f9.f2.x_loop:         ; preds = %output.curved.corrected.dem.f9.f2.x_loop, %output.curved.corrected.dem.f9.f2.y_loop
  %output.curved.corrected.dem.f9.f2.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.y_loop ], [ %output.curved.corrected.dem.f9.f2.x_nextvar, %output.curved.corrected.dem.f9.f2.x_loop ]
  %722 = mul i32 %output.curved.corrected.dem.f9.f2.y, 32
  %723 = add i32 %722, %output.curved.corrected.dem.f9.f2.x
  %memref95 = getelementptr i16* %output.curved.corrected.dem.f9.f2, i32 %723
  %724 = sdiv i32 %output.curved.corrected.dem.f9.f2.y, 2
  %725 = mul i32 %724, 32
  %726 = add i32 %725, %output.curved.corrected.dem.f9.f2.x
  %memref96 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0, i32 %726
  %727 = load i16* %memref96
  %728 = sdiv i32 %output.curved.corrected.dem.f9.f2.y, 2
  %729 = mul i32 %728, 32
  %730 = add i32 %729, %output.curved.corrected.dem.f9.f2.x
  %memref97 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f1, i32 %730
  %731 = load i16* %memref97
  %732 = srem i32 %output.curved.corrected.dem.f9.f2.y, 2
  %733 = add i32 %732, 2
  %734 = srem i32 %733, 2
  %735 = icmp eq i32 %734, 0
  %736 = select i1 %735, i16 %731, i16 %727
  store i16 %736, i16* %memref95
  %output.curved.corrected.dem.f9.f2.x_nextvar = add i32 %output.curved.corrected.dem.f9.f2.x, 1
  %737 = icmp ne i32 %output.curved.corrected.dem.f9.f2.x_nextvar, 32
  br i1 %737, label %output.curved.corrected.dem.f9.f2.x_loop, label %output.curved.corrected.dem.f9.f2.x_afterloop

output.curved.corrected.dem.f9.f2.x_afterloop:    ; preds = %output.curved.corrected.dem.f9.f2.x_loop
  %output.curved.corrected.dem.f9.f2.y_nextvar = add i32 %output.curved.corrected.dem.f9.f2.y, 1
  %738 = icmp ne i32 %output.curved.corrected.dem.f9.f2.y_nextvar, 16
  br i1 %738, label %output.curved.corrected.dem.f9.f2.y_loop, label %output.curved.corrected.dem.f9.f2.y_afterloop

output.curved.corrected.dem.f9.f2.y_afterloop:    ; preds = %output.curved.corrected.dem.f9.f2.x_afterloop
  br label %output.curved.corrected.dem.f9.f5.f3.y_loop

output.curved.corrected.dem.f9.f5.f3.y_loop:      ; preds = %output.curved.corrected.dem.f9.f5.f3.x_afterloop, %output.curved.corrected.dem.f9.f2.y_afterloop
  %output.curved.corrected.dem.f9.f5.f3.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f2.y_afterloop ], [ %output.curved.corrected.dem.f9.f5.f3.y_nextvar, %output.curved.corrected.dem.f9.f5.f3.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f5.f3.x_loop

output.curved.corrected.dem.f9.f5.f3.x_loop:      ; preds = %output.curved.corrected.dem.f9.f5.f3.x_loop, %output.curved.corrected.dem.f9.f5.f3.y_loop
  %output.curved.corrected.dem.f9.f5.f3.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.f3.y_loop ], [ %output.curved.corrected.dem.f9.f5.f3.x_nextvar, %output.curved.corrected.dem.f9.f5.f3.x_loop ]
  %739 = mul i32 %output.curved.corrected.dem.f9.f5.f3.y, 32
  %740 = add i32 %739, %output.curved.corrected.dem.f9.f5.f3.x
  %memref98 = getelementptr i16* %output.curved.corrected.dem.f9.f5.f3, i32 %740
  %741 = sdiv i32 %output.curved.corrected.dem.f9.f5.f3.x, 2
  %742 = mul i32 %output.curved.corrected.dem.f9.f5.f3.y, 18
  %743 = add i32 %742, %741
  %744 = add i32 %743, 19
  %memref99 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %744
  %745 = load i16* %memref99
  %746 = sdiv i32 %output.curved.corrected.dem.f9.f5.f3.x, 2
  %747 = mul i32 %output.curved.corrected.dem.f9.f5.f3.y, 17
  %748 = add i32 %747, %746
  %749 = add i32 %748, 17
  %memref100 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %749
  %750 = load i16* %memref100
  %751 = srem i32 %output.curved.corrected.dem.f9.f5.f3.x, 2
  %752 = add i32 %751, 2
  %753 = srem i32 %752, 2
  %754 = icmp eq i32 %753, 0
  %755 = select i1 %754, i16 %750, i16 %745
  store i16 %755, i16* %memref98
  %output.curved.corrected.dem.f9.f5.f3.x_nextvar = add i32 %output.curved.corrected.dem.f9.f5.f3.x, 1
  %756 = icmp ne i32 %output.curved.corrected.dem.f9.f5.f3.x_nextvar, 32
  br i1 %756, label %output.curved.corrected.dem.f9.f5.f3.x_loop, label %output.curved.corrected.dem.f9.f5.f3.x_afterloop

output.curved.corrected.dem.f9.f5.f3.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f5.f3.x_loop
  %output.curved.corrected.dem.f9.f5.f3.y_nextvar = add i32 %output.curved.corrected.dem.f9.f5.f3.y, 1
  %757 = icmp ne i32 %output.curved.corrected.dem.f9.f5.f3.y_nextvar, 8
  br i1 %757, label %output.curved.corrected.dem.f9.f5.f3.y_loop, label %output.curved.corrected.dem.f9.f5.f3.y_afterloop

output.curved.corrected.dem.f9.f5.f3.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f5.f3.x_afterloop
  br label %output.curved.corrected.dem.f9.f5.f4.y_loop

output.curved.corrected.dem.f9.f5.f4.y_loop:      ; preds = %output.curved.corrected.dem.f9.f5.f4.x_afterloop, %output.curved.corrected.dem.f9.f5.f3.y_afterloop
  %output.curved.corrected.dem.f9.f5.f4.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.f3.y_afterloop ], [ %output.curved.corrected.dem.f9.f5.f4.y_nextvar, %output.curved.corrected.dem.f9.f5.f4.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f5.f4.x_loop

output.curved.corrected.dem.f9.f5.f4.x_loop:      ; preds = %output.curved.corrected.dem.f9.f5.f4.x_loop, %output.curved.corrected.dem.f9.f5.f4.y_loop
  %output.curved.corrected.dem.f9.f5.f4.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.f4.y_loop ], [ %output.curved.corrected.dem.f9.f5.f4.x_nextvar, %output.curved.corrected.dem.f9.f5.f4.x_loop ]
  %758 = mul i32 %output.curved.corrected.dem.f9.f5.f4.y, 32
  %759 = add i32 %758, %output.curved.corrected.dem.f9.f5.f4.x
  %memref101 = getelementptr i16* %output.curved.corrected.dem.f9.f5.f4, i32 %759
  %760 = sdiv i32 %output.curved.corrected.dem.f9.f5.f4.x, 2
  %761 = mul i32 %output.curved.corrected.dem.f9.f5.f4.y, 17
  %762 = add i32 %761, %760
  %763 = add i32 %762, 1
  %memref102 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %763
  %764 = load i16* %memref102
  %765 = sdiv i32 %output.curved.corrected.dem.f9.f5.f4.x, 2
  %766 = mul i32 %output.curved.corrected.dem.f9.f5.f4.y, 18
  %767 = add i32 %766, %765
  %768 = add i32 %767, 19
  %memref103 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %768
  %769 = load i16* %memref103
  %770 = srem i32 %output.curved.corrected.dem.f9.f5.f4.x, 2
  %771 = add i32 %770, 2
  %772 = srem i32 %771, 2
  %773 = icmp eq i32 %772, 0
  %774 = select i1 %773, i16 %769, i16 %764
  store i16 %774, i16* %memref101
  %output.curved.corrected.dem.f9.f5.f4.x_nextvar = add i32 %output.curved.corrected.dem.f9.f5.f4.x, 1
  %775 = icmp ne i32 %output.curved.corrected.dem.f9.f5.f4.x_nextvar, 32
  br i1 %775, label %output.curved.corrected.dem.f9.f5.f4.x_loop, label %output.curved.corrected.dem.f9.f5.f4.x_afterloop

output.curved.corrected.dem.f9.f5.f4.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f5.f4.x_loop
  %output.curved.corrected.dem.f9.f5.f4.y_nextvar = add i32 %output.curved.corrected.dem.f9.f5.f4.y, 1
  %776 = icmp ne i32 %output.curved.corrected.dem.f9.f5.f4.y_nextvar, 8
  br i1 %776, label %output.curved.corrected.dem.f9.f5.f4.y_loop, label %output.curved.corrected.dem.f9.f5.f4.y_afterloop

output.curved.corrected.dem.f9.f5.f4.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f5.f4.x_afterloop
  br label %output.curved.corrected.dem.f9.f5.y_loop

output.curved.corrected.dem.f9.f5.y_loop:         ; preds = %output.curved.corrected.dem.f9.f5.x_afterloop, %output.curved.corrected.dem.f9.f5.f4.y_afterloop
  %output.curved.corrected.dem.f9.f5.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.f4.y_afterloop ], [ %output.curved.corrected.dem.f9.f5.y_nextvar, %output.curved.corrected.dem.f9.f5.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f5.x_loop

output.curved.corrected.dem.f9.f5.x_loop:         ; preds = %output.curved.corrected.dem.f9.f5.x_loop, %output.curved.corrected.dem.f9.f5.y_loop
  %output.curved.corrected.dem.f9.f5.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.y_loop ], [ %output.curved.corrected.dem.f9.f5.x_nextvar, %output.curved.corrected.dem.f9.f5.x_loop ]
  %777 = mul i32 %output.curved.corrected.dem.f9.f5.y, 32
  %778 = add i32 %777, %output.curved.corrected.dem.f9.f5.x
  %memref104 = getelementptr i16* %output.curved.corrected.dem.f9.f5, i32 %778
  %779 = sdiv i32 %output.curved.corrected.dem.f9.f5.y, 2
  %780 = mul i32 %779, 32
  %781 = add i32 %780, %output.curved.corrected.dem.f9.f5.x
  %memref105 = getelementptr i16* %output.curved.corrected.dem.f9.f5.f3, i32 %781
  %782 = load i16* %memref105
  %783 = sdiv i32 %output.curved.corrected.dem.f9.f5.y, 2
  %784 = mul i32 %783, 32
  %785 = add i32 %784, %output.curved.corrected.dem.f9.f5.x
  %memref106 = getelementptr i16* %output.curved.corrected.dem.f9.f5.f4, i32 %785
  %786 = load i16* %memref106
  %787 = srem i32 %output.curved.corrected.dem.f9.f5.y, 2
  %788 = add i32 %787, 2
  %789 = srem i32 %788, 2
  %790 = icmp eq i32 %789, 0
  %791 = select i1 %790, i16 %786, i16 %782
  store i16 %791, i16* %memref104
  %output.curved.corrected.dem.f9.f5.x_nextvar = add i32 %output.curved.corrected.dem.f9.f5.x, 1
  %792 = icmp ne i32 %output.curved.corrected.dem.f9.f5.x_nextvar, 32
  br i1 %792, label %output.curved.corrected.dem.f9.f5.x_loop, label %output.curved.corrected.dem.f9.f5.x_afterloop

output.curved.corrected.dem.f9.f5.x_afterloop:    ; preds = %output.curved.corrected.dem.f9.f5.x_loop
  %output.curved.corrected.dem.f9.f5.y_nextvar = add i32 %output.curved.corrected.dem.f9.f5.y, 1
  %793 = icmp ne i32 %output.curved.corrected.dem.f9.f5.y_nextvar, 16
  br i1 %793, label %output.curved.corrected.dem.f9.f5.y_loop, label %output.curved.corrected.dem.f9.f5.y_afterloop

output.curved.corrected.dem.f9.f5.y_afterloop:    ; preds = %output.curved.corrected.dem.f9.f5.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f6.b_b.y_loop

output.curved.corrected.dem.f9.f8.f6.b_b.y_loop:  ; preds = %output.curved.corrected.dem.f9.f8.f6.b_b.x_afterloop, %output.curved.corrected.dem.f9.f5.y_afterloop
  %output.curved.corrected.dem.f9.f8.f6.b_b.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f5.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f6.b_b.y_nextvar, %output.curved.corrected.dem.f9.f8.f6.b_b.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f6.b_b.x_loop

output.curved.corrected.dem.f9.f8.f6.b_b.x_loop:  ; preds = %output.curved.corrected.dem.f9.f8.f6.b_b.x_loop, %output.curved.corrected.dem.f9.f8.f6.b_b.y_loop
  %output.curved.corrected.dem.f9.f8.f6.b_b.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.b_b.y_loop ], [ %output.curved.corrected.dem.f9.f8.f6.b_b.x_nextvar, %output.curved.corrected.dem.f9.f8.f6.b_b.x_loop ]
  %794 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_b.y, 17
  %795 = add i32 %794, %output.curved.corrected.dem.f9.f8.f6.b_b.x
  %memref107 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %795
  %796 = mul i32 %output.blockidx, 16
  %797 = mul i32 %796, 2
  %798 = mul i32 %output.blockidx, 16
  %799 = add i32 %output.curved.corrected.dem.f9.f8.f6.b_b.x, %798
  %800 = mul i32 %799, 2
  %801 = mul i32 %output.blockidy, 8
  %802 = mul i32 %801, 2
  %803 = mul i32 %output.blockidy, 8
  %804 = add i32 %803, %output.curved.corrected.dem.f9.f8.f6.b_b.y
  %805 = mul i32 %804, 2
  %806 = sub i32 %805, %802
  %807 = mul i32 %806, 36
  %808 = add i32 %807, %800
  %809 = sub i32 %808, %797
  %810 = add i32 %809, 38
  %memref108 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb.denoised, i32 %810
  %811 = load i16* %memref108
  store i16 %811, i16* %memref107
  %output.curved.corrected.dem.f9.f8.f6.b_b.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.b_b.x, 1
  %812 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.b_b.x_nextvar, 17
  br i1 %812, label %output.curved.corrected.dem.f9.f8.f6.b_b.x_loop, label %output.curved.corrected.dem.f9.f8.f6.b_b.x_afterloop

output.curved.corrected.dem.f9.f8.f6.b_b.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_b.x_loop
  %output.curved.corrected.dem.f9.f8.f6.b_b.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.b_b.y, 1
  %813 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.b_b.y_nextvar, 9
  br i1 %813, label %output.curved.corrected.dem.f9.f8.f6.b_b.y_loop, label %output.curved.corrected.dem.f9.f8.f6.b_b.y_afterloop

output.curved.corrected.dem.f9.f8.f6.b_b.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_b.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f6.b_gb.y_loop

output.curved.corrected.dem.f9.f8.f6.b_gb.y_loop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_gb.x_afterloop, %output.curved.corrected.dem.f9.f8.f6.b_b.y_afterloop
  %output.curved.corrected.dem.f9.f8.f6.b_gb.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.b_b.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f6.b_gb.y_nextvar, %output.curved.corrected.dem.f9.f8.f6.b_gb.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop

output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop, %output.curved.corrected.dem.f9.f8.f6.b_gb.y_loop
  %output.curved.corrected.dem.f9.f8.f6.b_gb.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.b_gb.y_loop ], [ %output.curved.corrected.dem.f9.f8.f6.b_gb.x_nextvar, %output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop ]
  %814 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 16
  %815 = add i32 %814, %output.curved.corrected.dem.f9.f8.f6.b_gb.x
  %memref109 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_gb, i32 %815
  %816 = mul i32 %output.blockidx, 16
  %817 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 17
  %818 = mul i32 %output.blockidx, 16
  %819 = add i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.x, %818
  %820 = add i32 %819, %817
  %821 = sub i32 %820, %816
  %822 = add i32 %821, 18
  %memref110 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %822
  %823 = load i16* %memref110
  %824 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 17
  %825 = add i32 %824, %output.curved.corrected.dem.f9.f8.f6.b_gb.x
  %826 = add i32 %825, 17
  %memref111 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %826
  %827 = load i16* %memref111
  %828 = add i16 %827, %823
  %829 = sdiv i16 %828, 2
  %830 = mul i32 %output.blockidx, 16
  %831 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 17
  %832 = mul i32 %output.blockidx, 16
  %833 = add i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.x, %832
  %834 = add i32 %833, %831
  %835 = sub i32 %834, %830
  %836 = add i32 %835, 18
  %memref112 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %836
  %837 = load i16* %memref112
  %838 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 17
  %839 = add i32 %838, %output.curved.corrected.dem.f9.f8.f6.b_gb.x
  %840 = add i32 %839, 17
  %memref113 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %840
  %841 = load i16* %memref113
  %842 = add i16 %841, %837
  %843 = sdiv i16 %842, 2
  %844 = mul i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 18
  %845 = add i32 %844, %output.curved.corrected.dem.f9.f8.f6.b_gb.x
  %846 = add i32 %845, 19
  %memref114 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gb, i32 %846
  %847 = load i16* %memref114
  %848 = sub i16 %847, %843
  %849 = add i16 %848, %829
  store i16 %849, i16* %memref109
  %output.curved.corrected.dem.f9.f8.f6.b_gb.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.x, 1
  %850 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.x_nextvar, 16
  br i1 %850, label %output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop, label %output.curved.corrected.dem.f9.f8.f6.b_gb.x_afterloop

output.curved.corrected.dem.f9.f8.f6.b_gb.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_gb.x_loop
  %output.curved.corrected.dem.f9.f8.f6.b_gb.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y, 1
  %851 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.b_gb.y_nextvar, 8
  br i1 %851, label %output.curved.corrected.dem.f9.f8.f6.b_gb.y_loop, label %output.curved.corrected.dem.f9.f8.f6.b_gb.y_afterloop

output.curved.corrected.dem.f9.f8.f6.b_gb.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.b_gb.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f6.y_loop

output.curved.corrected.dem.f9.f8.f6.y_loop:      ; preds = %output.curved.corrected.dem.f9.f8.f6.x_afterloop, %output.curved.corrected.dem.f9.f8.f6.b_gb.y_afterloop
  %output.curved.corrected.dem.f9.f8.f6.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.b_gb.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f6.y_nextvar, %output.curved.corrected.dem.f9.f8.f6.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f6.x_loop

output.curved.corrected.dem.f9.f8.f6.x_loop:      ; preds = %output.curved.corrected.dem.f9.f8.f6.x_loop, %output.curved.corrected.dem.f9.f8.f6.y_loop
  %output.curved.corrected.dem.f9.f8.f6.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.y_loop ], [ %output.curved.corrected.dem.f9.f8.f6.x_nextvar, %output.curved.corrected.dem.f9.f8.f6.x_loop ]
  %852 = mul i32 %output.curved.corrected.dem.f9.f8.f6.y, 32
  %853 = add i32 %852, %output.curved.corrected.dem.f9.f8.f6.x
  %memref115 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6, i32 %853
  %854 = sdiv i32 %output.curved.corrected.dem.f9.f8.f6.x, 2
  %855 = mul i32 %output.curved.corrected.dem.f9.f8.f6.y, 16
  %856 = add i32 %855, %854
  %memref116 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_gb, i32 %856
  %857 = load i16* %memref116
  %858 = sdiv i32 %output.curved.corrected.dem.f9.f8.f6.x, 2
  %859 = mul i32 %output.curved.corrected.dem.f9.f8.f6.y, 17
  %860 = add i32 %859, %858
  %861 = add i32 %860, 17
  %memref117 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %861
  %862 = load i16* %memref117
  %863 = srem i32 %output.curved.corrected.dem.f9.f8.f6.x, 2
  %864 = add i32 %863, 2
  %865 = srem i32 %864, 2
  %866 = icmp eq i32 %865, 0
  %867 = select i1 %866, i16 %862, i16 %857
  store i16 %867, i16* %memref115
  %output.curved.corrected.dem.f9.f8.f6.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.x, 1
  %868 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.x_nextvar, 32
  br i1 %868, label %output.curved.corrected.dem.f9.f8.f6.x_loop, label %output.curved.corrected.dem.f9.f8.f6.x_afterloop

output.curved.corrected.dem.f9.f8.f6.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.x_loop
  %output.curved.corrected.dem.f9.f8.f6.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f6.y, 1
  %869 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f6.y_nextvar, 8
  br i1 %869, label %output.curved.corrected.dem.f9.f8.f6.y_loop, label %output.curved.corrected.dem.f9.f8.f6.y_afterloop

output.curved.corrected.dem.f9.f8.f6.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f6.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f7.b_gr.y_loop

output.curved.corrected.dem.f9.f8.f7.b_gr.y_loop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_gr.x_afterloop, %output.curved.corrected.dem.f9.f8.f6.y_afterloop
  %output.curved.corrected.dem.f9.f8.f7.b_gr.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f6.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f7.b_gr.y_nextvar, %output.curved.corrected.dem.f9.f8.f7.b_gr.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop

output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop, %output.curved.corrected.dem.f9.f8.f7.b_gr.y_loop
  %output.curved.corrected.dem.f9.f8.f7.b_gr.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.b_gr.y_loop ], [ %output.curved.corrected.dem.f9.f8.f7.b_gr.x_nextvar, %output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop ]
  %870 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 16
  %871 = add i32 %870, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %memref118 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7.b_gr, i32 %871
  %872 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 17
  %873 = add i32 %872, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %memref119 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %873
  %874 = load i16* %memref119
  %875 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 17
  %876 = add i32 %875, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %877 = add i32 %876, 17
  %memref120 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %877
  %878 = load i16* %memref120
  %879 = add i16 %878, %874
  %880 = sdiv i16 %879, 2
  %881 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 17
  %882 = add i32 %881, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %memref121 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %882
  %883 = load i16* %memref121
  %884 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 17
  %885 = add i32 %884, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %886 = add i32 %885, 17
  %memref122 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %886
  %887 = load i16* %memref122
  %888 = add i16 %887, %883
  %889 = sdiv i16 %888, 2
  %890 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 18
  %891 = add i32 %890, %output.curved.corrected.dem.f9.f8.f7.b_gr.x
  %892 = add i32 %891, 19
  %memref123 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b.g_gr, i32 %892
  %893 = load i16* %memref123
  %894 = sub i16 %893, %889
  %895 = add i16 %894, %880
  store i16 %895, i16* %memref118
  %output.curved.corrected.dem.f9.f8.f7.b_gr.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.x, 1
  %896 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.x_nextvar, 16
  br i1 %896, label %output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop, label %output.curved.corrected.dem.f9.f8.f7.b_gr.x_afterloop

output.curved.corrected.dem.f9.f8.f7.b_gr.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_gr.x_loop
  %output.curved.corrected.dem.f9.f8.f7.b_gr.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y, 1
  %897 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.b_gr.y_nextvar, 8
  br i1 %897, label %output.curved.corrected.dem.f9.f8.f7.b_gr.y_loop, label %output.curved.corrected.dem.f9.f8.f7.b_gr.y_afterloop

output.curved.corrected.dem.f9.f8.f7.b_gr.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_gr.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f7.b_r.y_loop

output.curved.corrected.dem.f9.f8.f7.b_r.y_loop:  ; preds = %output.curved.corrected.dem.f9.f8.f7.b_r.x_afterloop, %output.curved.corrected.dem.f9.f8.f7.b_gr.y_afterloop
  %output.curved.corrected.dem.f9.f8.f7.b_r.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.b_gr.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f7.b_r.y_nextvar, %output.curved.corrected.dem.f9.f8.f7.b_r.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f7.b_r.x_loop

output.curved.corrected.dem.f9.f8.f7.b_r.x_loop:  ; preds = %output.curved.corrected.dem.f9.f8.f7.b_r.x_loop, %output.curved.corrected.dem.f9.f8.f7.b_r.y_loop
  %output.curved.corrected.dem.f9.f8.f7.b_r.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.b_r.y_loop ], [ %output.curved.corrected.dem.f9.f8.f7.b_r.x_nextvar, %output.curved.corrected.dem.f9.f8.f7.b_r.x_loop ]
  %898 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 16
  %899 = add i32 %898, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref124 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7.b_r, i32 %899
  %900 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %901 = add i32 %900, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref125 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %901
  %902 = load i16* %memref125
  %903 = mul i32 %output.blockidx, 16
  %904 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %905 = mul i32 %output.blockidx, 16
  %906 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %905
  %907 = add i32 %906, %904
  %908 = sub i32 %907, %903
  %909 = add i32 %908, 18
  %memref126 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %909
  %910 = load i16* %memref126
  %911 = add i16 %910, %902
  %912 = sdiv i16 %911, 2
  %913 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %914 = add i32 %913, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref127 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %914
  %915 = load i16* %memref127
  %916 = mul i32 %output.blockidx, 16
  %917 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %918 = mul i32 %output.blockidx, 16
  %919 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %918
  %920 = add i32 %919, %917
  %921 = sub i32 %920, %916
  %922 = add i32 %921, 18
  %memref128 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %922
  %923 = load i16* %memref128
  %924 = add i16 %923, %915
  %925 = sdiv i16 %924, 2
  %926 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %927 = add i32 %926, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %928 = add i32 %927, 1
  %memref129 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %928
  %929 = load i16* %memref129
  %930 = sub i16 %929, %925
  %931 = add i16 %930, %912
  %932 = mul i32 %output.blockidx, 16
  %933 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %934 = mul i32 %output.blockidx, 16
  %935 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %934
  %936 = add i32 %935, %933
  %937 = sub i32 %936, %932
  %938 = add i32 %937, 1
  %memref130 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %938
  %939 = load i16* %memref130
  %940 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %941 = add i32 %940, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %942 = add i32 %941, 17
  %memref131 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %942
  %943 = load i16* %memref131
  %944 = add i16 %943, %939
  %945 = sdiv i16 %944, 2
  %946 = mul i32 %output.blockidx, 16
  %947 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %948 = mul i32 %output.blockidx, 16
  %949 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %948
  %950 = add i32 %949, %947
  %951 = sub i32 %950, %946
  %952 = add i32 %951, 1
  %memref132 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %952
  %953 = load i16* %memref132
  %954 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %955 = add i32 %954, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %956 = add i32 %955, 17
  %memref133 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_b, i32 %956
  %957 = load i16* %memref133
  %958 = add i16 %957, %953
  %959 = sdiv i16 %958, 2
  %960 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %961 = add i32 %960, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %962 = add i32 %961, 1
  %memref134 = getelementptr i16* %output.curved.corrected.dem.f9.f2.f0.r_b.g_r, i32 %962
  %963 = load i16* %memref134
  %964 = sub i16 %963, %959
  %965 = add i16 %964, %945
  %966 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %967 = add i32 %966, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref135 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %967
  %968 = load i16* %memref135
  %969 = mul i32 %output.blockidx, 16
  %970 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %971 = mul i32 %output.blockidx, 16
  %972 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %971
  %973 = add i32 %972, %970
  %974 = sub i32 %973, %969
  %975 = add i32 %974, 18
  %memref136 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %975
  %976 = load i16* %memref136
  %977 = sub i16 %976, %968
  %978 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %979 = add i32 %978, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref137 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %979
  %980 = load i16* %memref137
  %981 = mul i32 %output.blockidx, 16
  %982 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %983 = mul i32 %output.blockidx, 16
  %984 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %983
  %985 = add i32 %984, %982
  %986 = sub i32 %985, %981
  %987 = add i32 %986, 18
  %memref138 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %987
  %988 = load i16* %memref138
  %989 = sub i16 %988, %980
  %990 = sub i16 0, %989
  %991 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %992 = add i32 %991, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %memref139 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %992
  %993 = load i16* %memref139
  %994 = mul i32 %output.blockidx, 16
  %995 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %996 = mul i32 %output.blockidx, 16
  %997 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %996
  %998 = add i32 %997, %995
  %999 = sub i32 %998, %994
  %1000 = add i32 %999, 18
  %memref140 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1000
  %1001 = load i16* %memref140
  %1002 = sub i16 %1001, %993
  %1003 = icmp slt i16 %1002, 0
  %1004 = select i1 %1003, i16 %990, i16 %977
  %1005 = mul i32 %output.blockidx, 16
  %1006 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1007 = mul i32 %output.blockidx, 16
  %1008 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %1007
  %1009 = add i32 %1008, %1006
  %1010 = sub i32 %1009, %1005
  %1011 = add i32 %1010, 1
  %memref141 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1011
  %1012 = load i16* %memref141
  %1013 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1014 = add i32 %1013, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %1015 = add i32 %1014, 17
  %memref142 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1015
  %1016 = load i16* %memref142
  %1017 = sub i16 %1016, %1012
  %1018 = mul i32 %output.blockidx, 16
  %1019 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1020 = mul i32 %output.blockidx, 16
  %1021 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %1020
  %1022 = add i32 %1021, %1019
  %1023 = sub i32 %1022, %1018
  %1024 = add i32 %1023, 1
  %memref143 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1024
  %1025 = load i16* %memref143
  %1026 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1027 = add i32 %1026, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %1028 = add i32 %1027, 17
  %memref144 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1028
  %1029 = load i16* %memref144
  %1030 = sub i16 %1029, %1025
  %1031 = sub i16 0, %1030
  %1032 = mul i32 %output.blockidx, 16
  %1033 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1034 = mul i32 %output.blockidx, 16
  %1035 = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, %1034
  %1036 = add i32 %1035, %1033
  %1037 = sub i32 %1036, %1032
  %1038 = add i32 %1037, 1
  %memref145 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1038
  %1039 = load i16* %memref145
  %1040 = mul i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 17
  %1041 = add i32 %1040, %output.curved.corrected.dem.f9.f8.f7.b_r.x
  %1042 = add i32 %1041, 17
  %memref146 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6.b_b, i32 %1042
  %1043 = load i16* %memref146
  %1044 = sub i16 %1043, %1039
  %1045 = icmp slt i16 %1044, 0
  %1046 = select i1 %1045, i16 %1031, i16 %1017
  %1047 = icmp slt i16 %1046, %1004
  %1048 = select i1 %1047, i16 %965, i16 %931
  store i16 %1048, i16* %memref124
  %output.curved.corrected.dem.f9.f8.f7.b_r.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x, 1
  %1049 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.b_r.x_nextvar, 16
  br i1 %1049, label %output.curved.corrected.dem.f9.f8.f7.b_r.x_loop, label %output.curved.corrected.dem.f9.f8.f7.b_r.x_afterloop

output.curved.corrected.dem.f9.f8.f7.b_r.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_r.x_loop
  %output.curved.corrected.dem.f9.f8.f7.b_r.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y, 1
  %1050 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.b_r.y_nextvar, 8
  br i1 %1050, label %output.curved.corrected.dem.f9.f8.f7.b_r.y_loop, label %output.curved.corrected.dem.f9.f8.f7.b_r.y_afterloop

output.curved.corrected.dem.f9.f8.f7.b_r.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.b_r.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.f7.y_loop

output.curved.corrected.dem.f9.f8.f7.y_loop:      ; preds = %output.curved.corrected.dem.f9.f8.f7.x_afterloop, %output.curved.corrected.dem.f9.f8.f7.b_r.y_afterloop
  %output.curved.corrected.dem.f9.f8.f7.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.b_r.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.f7.y_nextvar, %output.curved.corrected.dem.f9.f8.f7.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.f7.x_loop

output.curved.corrected.dem.f9.f8.f7.x_loop:      ; preds = %output.curved.corrected.dem.f9.f8.f7.x_loop, %output.curved.corrected.dem.f9.f8.f7.y_loop
  %output.curved.corrected.dem.f9.f8.f7.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.y_loop ], [ %output.curved.corrected.dem.f9.f8.f7.x_nextvar, %output.curved.corrected.dem.f9.f8.f7.x_loop ]
  %1051 = mul i32 %output.curved.corrected.dem.f9.f8.f7.y, 32
  %1052 = add i32 %1051, %output.curved.corrected.dem.f9.f8.f7.x
  %memref147 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7, i32 %1052
  %1053 = sdiv i32 %output.curved.corrected.dem.f9.f8.f7.x, 2
  %1054 = mul i32 %output.curved.corrected.dem.f9.f8.f7.y, 16
  %1055 = add i32 %1054, %1053
  %memref148 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7.b_r, i32 %1055
  %1056 = load i16* %memref148
  %1057 = sdiv i32 %output.curved.corrected.dem.f9.f8.f7.x, 2
  %1058 = mul i32 %output.curved.corrected.dem.f9.f8.f7.y, 16
  %1059 = add i32 %1058, %1057
  %memref149 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7.b_gr, i32 %1059
  %1060 = load i16* %memref149
  %1061 = srem i32 %output.curved.corrected.dem.f9.f8.f7.x, 2
  %1062 = add i32 %1061, 2
  %1063 = srem i32 %1062, 2
  %1064 = icmp eq i32 %1063, 0
  %1065 = select i1 %1064, i16 %1060, i16 %1056
  store i16 %1065, i16* %memref147
  %output.curved.corrected.dem.f9.f8.f7.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.x, 1
  %1066 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.x_nextvar, 32
  br i1 %1066, label %output.curved.corrected.dem.f9.f8.f7.x_loop, label %output.curved.corrected.dem.f9.f8.f7.x_afterloop

output.curved.corrected.dem.f9.f8.f7.x_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.x_loop
  %output.curved.corrected.dem.f9.f8.f7.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.f7.y, 1
  %1067 = icmp ne i32 %output.curved.corrected.dem.f9.f8.f7.y_nextvar, 8
  br i1 %1067, label %output.curved.corrected.dem.f9.f8.f7.y_loop, label %output.curved.corrected.dem.f9.f8.f7.y_afterloop

output.curved.corrected.dem.f9.f8.f7.y_afterloop: ; preds = %output.curved.corrected.dem.f9.f8.f7.x_afterloop
  br label %output.curved.corrected.dem.f9.f8.y_loop

output.curved.corrected.dem.f9.f8.y_loop:         ; preds = %output.curved.corrected.dem.f9.f8.x_afterloop, %output.curved.corrected.dem.f9.f8.f7.y_afterloop
  %output.curved.corrected.dem.f9.f8.y = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.f7.y_afterloop ], [ %output.curved.corrected.dem.f9.f8.y_nextvar, %output.curved.corrected.dem.f9.f8.x_afterloop ]
  br label %output.curved.corrected.dem.f9.f8.x_loop

output.curved.corrected.dem.f9.f8.x_loop:         ; preds = %output.curved.corrected.dem.f9.f8.x_loop, %output.curved.corrected.dem.f9.f8.y_loop
  %output.curved.corrected.dem.f9.f8.x = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.y_loop ], [ %output.curved.corrected.dem.f9.f8.x_nextvar, %output.curved.corrected.dem.f9.f8.x_loop ]
  %1068 = mul i32 %output.curved.corrected.dem.f9.f8.y, 32
  %1069 = add i32 %1068, %output.curved.corrected.dem.f9.f8.x
  %memref150 = getelementptr i16* %output.curved.corrected.dem.f9.f8, i32 %1069
  %1070 = sdiv i32 %output.curved.corrected.dem.f9.f8.y, 2
  %1071 = mul i32 %1070, 32
  %1072 = add i32 %1071, %output.curved.corrected.dem.f9.f8.x
  %memref151 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f6, i32 %1072
  %1073 = load i16* %memref151
  %1074 = sdiv i32 %output.curved.corrected.dem.f9.f8.y, 2
  %1075 = mul i32 %1074, 32
  %1076 = add i32 %1075, %output.curved.corrected.dem.f9.f8.x
  %memref152 = getelementptr i16* %output.curved.corrected.dem.f9.f8.f7, i32 %1076
  %1077 = load i16* %memref152
  %1078 = srem i32 %output.curved.corrected.dem.f9.f8.y, 2
  %1079 = add i32 %1078, 2
  %1080 = srem i32 %1079, 2
  %1081 = icmp eq i32 %1080, 0
  %1082 = select i1 %1081, i16 %1077, i16 %1073
  store i16 %1082, i16* %memref150
  %output.curved.corrected.dem.f9.f8.x_nextvar = add i32 %output.curved.corrected.dem.f9.f8.x, 1
  %1083 = icmp ne i32 %output.curved.corrected.dem.f9.f8.x_nextvar, 32
  br i1 %1083, label %output.curved.corrected.dem.f9.f8.x_loop, label %output.curved.corrected.dem.f9.f8.x_afterloop

output.curved.corrected.dem.f9.f8.x_afterloop:    ; preds = %output.curved.corrected.dem.f9.f8.x_loop
  %output.curved.corrected.dem.f9.f8.y_nextvar = add i32 %output.curved.corrected.dem.f9.f8.y, 1
  %1084 = icmp ne i32 %output.curved.corrected.dem.f9.f8.y_nextvar, 16
  br i1 %1084, label %output.curved.corrected.dem.f9.f8.y_loop, label %output.curved.corrected.dem.f9.f8.y_afterloop

output.curved.corrected.dem.f9.f8.y_afterloop:    ; preds = %output.curved.corrected.dem.f9.f8.x_afterloop
  br label %output.curved.corrected.dem.f9.v0_loop

output.curved.corrected.dem.f9.v0_loop:           ; preds = %output.curved.corrected.dem.f9.y_afterloop, %output.curved.corrected.dem.f9.f8.y_afterloop
  %output.curved.corrected.dem.f9.v0 = phi i32 [ 0, %output.curved.corrected.dem.f9.f8.y_afterloop ], [ %output.curved.corrected.dem.f9.v0_nextvar, %output.curved.corrected.dem.f9.y_afterloop ]
  br label %output.curved.corrected.dem.f9.y_loop

output.curved.corrected.dem.f9.y_loop:            ; preds = %output.curved.corrected.dem.f9.x_afterloop, %output.curved.corrected.dem.f9.v0_loop
  %output.curved.corrected.dem.f9.y = phi i32 [ 0, %output.curved.corrected.dem.f9.v0_loop ], [ %output.curved.corrected.dem.f9.y_nextvar, %output.curved.corrected.dem.f9.x_afterloop ]
  br label %output.curved.corrected.dem.f9.x_loop

output.curved.corrected.dem.f9.x_loop:            ; preds = %output.curved.corrected.dem.f9.x_loop, %output.curved.corrected.dem.f9.y_loop
  %output.curved.corrected.dem.f9.x = phi i32 [ 0, %output.curved.corrected.dem.f9.y_loop ], [ %output.curved.corrected.dem.f9.x_nextvar, %output.curved.corrected.dem.f9.x_loop ]
  %1085 = mul i32 %output.curved.corrected.dem.f9.v0, 16
  %1086 = add i32 %1085, %output.curved.corrected.dem.f9.y
  %1087 = mul i32 %1086, 32
  %1088 = add i32 %1087, %output.curved.corrected.dem.f9.x
  %memref153 = getelementptr i16* %output.curved.corrected.dem.f9, i32 %1088
  %1089 = mul i32 %output.curved.corrected.dem.f9.y, 32
  %1090 = add i32 %1089, %output.curved.corrected.dem.f9.x
  %memref154 = getelementptr i16* %output.curved.corrected.dem.f9.f2, i32 %1090
  %1091 = load i16* %memref154
  %1092 = mul i32 %output.curved.corrected.dem.f9.y, 32
  %1093 = add i32 %1092, %output.curved.corrected.dem.f9.x
  %memref155 = getelementptr i16* %output.curved.corrected.dem.f9.f5, i32 %1093
  %1094 = load i16* %memref155
  %1095 = icmp eq i32 %output.curved.corrected.dem.f9.v0, 1
  %1096 = select i1 %1095, i16 %1094, i16 %1091
  %1097 = mul i32 %output.curved.corrected.dem.f9.y, 32
  %1098 = add i32 %1097, %output.curved.corrected.dem.f9.x
  %memref156 = getelementptr i16* %output.curved.corrected.dem.f9.f8, i32 %1098
  %1099 = load i16* %memref156
  %1100 = icmp eq i32 %output.curved.corrected.dem.f9.v0, 2
  %1101 = select i1 %1100, i16 %1099, i16 %1096
  store i16 %1101, i16* %memref153
  %output.curved.corrected.dem.f9.x_nextvar = add i32 %output.curved.corrected.dem.f9.x, 1
  %1102 = icmp ne i32 %output.curved.corrected.dem.f9.x_nextvar, 32
  br i1 %1102, label %output.curved.corrected.dem.f9.x_loop, label %output.curved.corrected.dem.f9.x_afterloop

output.curved.corrected.dem.f9.x_afterloop:       ; preds = %output.curved.corrected.dem.f9.x_loop
  %output.curved.corrected.dem.f9.y_nextvar = add i32 %output.curved.corrected.dem.f9.y, 1
  %1103 = icmp ne i32 %output.curved.corrected.dem.f9.y_nextvar, 16
  br i1 %1103, label %output.curved.corrected.dem.f9.y_loop, label %output.curved.corrected.dem.f9.y_afterloop

output.curved.corrected.dem.f9.y_afterloop:       ; preds = %output.curved.corrected.dem.f9.x_afterloop
  %output.curved.corrected.dem.f9.v0_nextvar = add i32 %output.curved.corrected.dem.f9.v0, 1
  %1104 = icmp ne i32 %output.curved.corrected.dem.f9.v0_nextvar, 4
  br i1 %1104, label %output.curved.corrected.dem.f9.v0_loop, label %output.curved.corrected.dem.f9.v0_afterloop

output.curved.corrected.dem.f9.v0_afterloop:      ; preds = %output.curved.corrected.dem.f9.y_afterloop
  %extern_llvm.ptx.read.tid.y = call i32 @llvm.ptx.read.tid.y()
  %1105 = icmp slt i32 %extern_llvm.ptx.read.tid.y, 16
  br i1 %1105, label %output.threadidy_simt_loop, label %output.threadidy_simt_afterloop

output.threadidy_simt_loop:                       ; preds = %output.curved.corrected.dem.f9.v0_afterloop
  %extern_llvm.ptx.read.tid.y157 = call i32 @llvm.ptx.read.tid.y()
  %output.threadidy = add i32 %extern_llvm.ptx.read.tid.y157, 0
  %extern_llvm.ptx.read.tid.x = call i32 @llvm.ptx.read.tid.x()
  %1106 = icmp slt i32 %extern_llvm.ptx.read.tid.x, 32
  br i1 %1106, label %output.threadidx_simt_loop, label %output.threadidx_simt_afterloop

output.threadidy_simt_afterloop:                  ; preds = %output.threadidx_simt_afterloop, %output.curved.corrected.dem.f9.v0_afterloop
  br label %output.blockidx_simt_afterloop

output.threadidx_simt_loop:                       ; preds = %output.threadidy_simt_loop
  %extern_llvm.ptx.read.tid.x158 = call i32 @llvm.ptx.read.tid.x()
  %output.threadidx = add i32 %extern_llvm.ptx.read.tid.x158, 0
  br label %output.c_loop

output.threadidx_simt_afterloop:                  ; preds = %output.c_afterloop, %output.threadidy_simt_loop
  br label %output.threadidy_simt_afterloop

output.c_loop:                                    ; preds = %output.c_loop, %output.threadidx_simt_loop
  %output.c = phi i32 [ 0, %output.threadidx_simt_loop ], [ %output.c_nextvar, %output.c_loop ]
  %memref_elem_ptr159 = bitcast i8* %.result to i16*
  %1107 = mul i32 %output.blockidx, 32
  %1108 = add i32 %1107, %output.threadidx
  %1109 = mul i32 %output.blockidy, 16
  %1110 = add i32 %1109, %output.threadidy
  %1111 = add i32 %.result.dim.2, 2
  %1112 = sdiv i32 %1111, 3
  %1113 = mul i32 %1112, 3
  %1114 = mul i32 %output.nilc, %1113
  %1115 = add i32 %1114, %output.c
  %1116 = add i32 %.result.dim.1, 15
  %1117 = sdiv i32 %1116, 16
  %1118 = mul i32 %1117, 16
  %1119 = mul i32 %1118, %1115
  %1120 = add i32 %1119, %1110
  %1121 = add i32 %.result.dim.0, 31
  %1122 = sdiv i32 %1121, 32
  %1123 = mul i32 %1122, 32
  %1124 = mul i32 %1123, %1120
  %1125 = add i32 %1124, %1108
  %memref160 = getelementptr i16* %memref_elem_ptr159, i32 %1125
  %1126 = mul i32 %output.blockidx, 32
  %output.curved.x = add i32 %1126, %output.threadidx
  %1127 = mul i32 %output.blockidy, 16
  %output.curved.y = add i32 %1127, %output.threadidy
  %1128 = add i32 %.result.dim.2, 2
  %1129 = sdiv i32 %1128, 3
  %1130 = mul i32 %1129, 3
  %1131 = mul i32 %output.nilc, %1130
  %output.curved.c = add i32 %1131, %output.c
  %memref_elem_ptr161 = bitcast i8* %output.curved.curve to i16*
  %memref_elem_ptr162 = bitcast i8* %output.curved.corrected.matrix to float*
  %1132 = sub i32 %output.curved.c, %output.curved.corrected.matrix.y.min
  %1133 = mul i32 %1132, 4
  %1134 = add i32 %1133, 3
  %memref163 = getelementptr float* %memref_elem_ptr162, i32 %1134
  %1135 = load float* %memref163
  %1136 = mul i32 %output.blockidx, 32
  %1137 = mul i32 %output.blockidy, 16
  %1138 = sub i32 %output.curved.y, %1137
  %1139 = mul i32 %1138, 32
  %1140 = add i32 %1139, %output.curved.x
  %1141 = sub i32 %1140, %1136
  %1142 = add i32 %1141, 1536
  %memref164 = getelementptr i16* %output.curved.corrected.dem.f9, i32 %1142
  %1143 = load i16* %memref164
  %1144 = sitofp i16 %1143 to float
  %memref_elem_ptr165 = bitcast i8* %output.curved.corrected.matrix to float*
  %1145 = sub i32 %output.curved.c, %output.curved.corrected.matrix.y.min
  %1146 = mul i32 %1145, 4
  %1147 = add i32 %1146, 3
  %memref166 = getelementptr float* %memref_elem_ptr165, i32 %1147
  %1148 = load float* %memref166
  %1149 = fmul float %1148, %1144
  %1150 = mul i32 %output.blockidx, 32
  %1151 = mul i32 %output.blockidy, 16
  %1152 = sub i32 %output.curved.y, %1151
  %1153 = mul i32 %1152, 32
  %1154 = add i32 %1153, %output.curved.x
  %1155 = sub i32 %1154, %1150
  %1156 = add i32 %1155, 1024
  %memref167 = getelementptr i16* %output.curved.corrected.dem.f9, i32 %1156
  %1157 = load i16* %memref167
  %1158 = sitofp i16 %1157 to float
  %memref_elem_ptr168 = bitcast i8* %output.curved.corrected.matrix to float*
  %1159 = sub i32 %output.curved.c, %output.curved.corrected.matrix.y.min
  %1160 = mul i32 %1159, 4
  %1161 = add i32 %1160, 2
  %memref169 = getelementptr float* %memref_elem_ptr168, i32 %1161
  %1162 = load float* %memref169
  %1163 = fmul float %1162, %1158
  %1164 = mul i32 %output.blockidx, 32
  %1165 = mul i32 %output.blockidy, 16
  %1166 = sub i32 %output.curved.y, %1165
  %1167 = mul i32 %1166, 32
  %1168 = add i32 %1167, %output.curved.x
  %1169 = sub i32 %1168, %1164
  %1170 = add i32 %1169, 512
  %memref170 = getelementptr i16* %output.curved.corrected.dem.f9, i32 %1170
  %1171 = load i16* %memref170
  %1172 = sitofp i16 %1171 to float
  %memref_elem_ptr171 = bitcast i8* %output.curved.corrected.matrix to float*
  %1173 = sub i32 %output.curved.c, %output.curved.corrected.matrix.y.min
  %1174 = mul i32 %1173, 4
  %1175 = add i32 %1174, 1
  %memref172 = getelementptr float* %memref_elem_ptr171, i32 %1175
  %1176 = load float* %memref172
  %1177 = fmul float %1176, %1172
  %1178 = mul i32 %output.blockidx, 32
  %1179 = mul i32 %output.blockidy, 16
  %1180 = sub i32 %output.curved.y, %1179
  %1181 = mul i32 %1180, 32
  %1182 = add i32 %1181, %output.curved.x
  %1183 = sub i32 %1182, %1178
  %memref173 = getelementptr i16* %output.curved.corrected.dem.f9, i32 %1183
  %1184 = load i16* %memref173
  %1185 = sitofp i16 %1184 to float
  %memref_elem_ptr174 = bitcast i8* %output.curved.corrected.matrix to float*
  %1186 = sub i32 %output.curved.c, %output.curved.corrected.matrix.y.min
  %1187 = mul i32 %1186, 4
  %memref175 = getelementptr float* %memref_elem_ptr174, i32 %1187
  %1188 = load float* %memref175
  %1189 = fmul float %1188, %1185
  %1190 = fadd float %1189, %1177
  %1191 = fadd float %1190, %1163
  %1192 = fadd float %1191, %1149
  %1193 = fadd float %1192, %1135
  %1194 = fptosi float %1193 to i16
  %1195 = sext i16 %1194 to i32
  %1196 = icmp slt i32 %1195, 1023
  %1197 = select i1 %1196, i32 %1195, i32 1023
  %1198 = icmp sgt i32 %1197, 0
  %1199 = select i1 %1198, i32 %1197, i32 0
  %1200 = sub i32 %1199, 0
  %memref176 = getelementptr i16* %memref_elem_ptr161, i32 %1200
  %1201 = load i16* %memref176
  store i16 %1201, i16* %memref160
  %output.c_nextvar = add i32 %output.c, 1
  %1202 = icmp ne i32 %output.c_nextvar, 3
  br i1 %1202, label %output.c_loop, label %output.c_afterloop

output.c_afterloop:                               ; preds = %output.c_loop
  br label %output.threadidx_simt_afterloop
}

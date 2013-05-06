; These work
declare float @llvm.cos.f32(float)
declare float @llvm.sqrt.f32(float)

; These don't work
;declare float @llvm.pow.f32(float,float)
;declare float @llvm.exp.f32(float)
;declare float @llvm.log.f32(float)

; these would work, but *calls* seem to barf!
define i32 @floor(float %x) nounwind readnone alwaysinline {
  %1 = fptosi float %x to i32
  ret i32 %1
}

; these would work, but *calls* seem to barf!
define i32 @ceil(float %x) nounwind readnone alwaysinline {
  %1 = fptosi float %x to i32
  %2 = sitofp i32 %1 to float
  %3 = fcmp oeq float %2, %x
  br i1 %3, label %6, label %4

; <label>:4                                       ; preds = %0
  %5 = fadd float %x, 1.000000e+00
  br label %6

; <label>:6                                       ; preds = %4, %0
  %.0.in = phi float [ %5, %4 ], [ %x, %0 ]
  %.0 = fptosi float %.0.in to i32
  ret i32 %.0
}

define ptx_kernel void @f(float %in, float* %fout, i32* %iout) {
  %1 = call float @llvm.cos.f32(float %in)
  %2 = call float @llvm.sqrt.f32(float %1)
  %3 = call i32 @floor(float %1)
  ;%3 = call float @llvm.pow.f32(float %2, float %1)
  ;%4 = call float @llvm.exp.f32(float %3)
  ;%5 = call float @llvm.log.f32(float %4)
  store float %2, float* %fout
  store i32 %3, i32* %iout
  ret void
}

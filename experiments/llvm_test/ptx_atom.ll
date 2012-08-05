@shmem = addrspace(4) global [5 x i32] zeroinitializer

declare void @llvm.ptx.red.global.add.s32(i32*, i32)
declare void @llvm.ptx.red.global.add.f32(float*, float)
declare void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)*, i32)

define ptx_kernel void @f(i32 %in, i32* %out, float %fin, float* %fout) {
  call void @llvm.ptx.red.global.add.s32(i32* %out, i32 %in)
  call void @llvm.ptx.red.global.add.f32(float* %fout, float %fin)
  %sh = getelementptr [5 x i32] addrspace(4)* @shmem, i32 0, i32 0
  call void @llvm.ptx.red.shared.add.s32(i32 addrspace(4)* %sh, i32 %in)
  ret void
}

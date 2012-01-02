
declare <16 x i8>  @llvm.arm.neon.vld1.v16i8(i8*, i32) nounwind readonly
declare void  @llvm.arm.neon.vst1.v16i8(i8*, <16 x i8>, i32) nounwind readonly
declare <8 x i8>  @llvm.arm.neon.vld1.v8i8(i8*, i32) nounwind readonly
declare void  @llvm.arm.neon.vst1.v8i8(i8*, <8 x i8>, i32) nounwind readonly

define <16 x i8> @unaligned_load_128(i8 * nocapture %ptr) nounwind readonly alwaysinline {
  %1 = call <16 x i8> @llvm.arm.neon.vld1.v16i8(i8 * %ptr, i32 1)
  ret <16 x i8> %1
}

define <8 x i8> @unaligned_load_64(i8 * nocapture %ptr) nounwind readonly alwaysinline {
  %1 = call <8 x i8> @llvm.arm.neon.vld1.v8i8(i8 * %ptr, i32 1)
  ret <8 x i8> %1
}

define void @unaligned_store_128(<16 x i8> %arg, i8 * nocapture %ptr) nounwind alwaysinline {
  call void @llvm.arm.neon.vst1.v16i8(i8 * %ptr, <16 x i8> %arg, i32 1)
  ret void
}

define void @unaligned_store_64(<8 x i8> %arg, i8 * nocapture %ptr) nounwind alwaysinline {
  call void @llvm.arm.neon.vst1.v8i8(i8 * %ptr, <8 x i8> %arg, i32 1)
  ret void
}

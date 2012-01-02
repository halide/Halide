
declare <16 x i8>  @llvm.arm.neon.vld1.v16i8(i8*, i32) nounwind readonly
declare void  @llvm.arm.neon.vst1.v16i8(i8*, <16 x i8>, i32) nounwind readonly

define <16 x i8> @unaligned_load_128(i8 * nocapture %ptr) nounwind readonly alwaysinline {
  %1 = call <16 x i8> @llvm.arm.neon.vld1.v16i8(i8 * %ptr, i32 1)
  ret <16 x i8> %1
;  %1 = bitcast i8 * %ptr to <16 x i8> *
;  %2 = load <16 x i8>* %1, align 4
;  ret <16 x i8> %2
}

define void @unaligned_store_128(<16 x i8> %arg, i8 * nocapture %ptr) nounwind alwaysinline {
  call void @llvm.arm.neon.vst1.v16i8(i8 * %ptr, <16 x i8> %arg, i32 1)
  ret void
;  %1 = bitcast i8 * %ptr to <16 x i8> *
;  store <16 x i8> %arg, <16 x i8>* %1, align 1
;  ret void
}

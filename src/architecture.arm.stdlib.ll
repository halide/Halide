

define <16 x i8> @unaligned_load_128(<16 x i8> * nocapture %ptr) nounwind readonly {
  %1 = load <16 x i8>* %ptr, align 1
  ret <16 x i8> %1
}

define void @unaligned_store_128(<16 x i8> %arg, <16 x i8> * nocapture %ptr) nounwind {
  store <16 x i8> %arg, <16 x i8>* %ptr, align 1
  ret void
}
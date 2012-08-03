
define <4 x i32> @blend_sse41(<4 x i32> %x, <4 x i32> %y) {
  %mask = icmp sgt <4 x i32> %x, %y
  %mask2 = sext <4 x i1> %mask to <4 x i32>
  %mask3 = bitcast <4 x i32> %mask2 to <16 x i8>
  %x2 = bitcast <4 x i32> %x to <16 x i8>
  %y2 = bitcast <4 x i32> %y to <16 x i8>
  %blended = call <16 x i8> @llvm.x86.sse41.pblendvb(<16 x i8> %x2, <16 x i8> %y2, <16 x i8> %mask3)
  %blended2 = bitcast <16 x i8> %blended to <4 x i32>

  ret <4 x i32> %blended2
}
declare <16 x i8> @llvm.x86.sse41.pblendvb(<16 x i8>, <16 x i8>, <16 x i8>) nounwind readnone

; This pattern generates the proper vbsl op on NEON, but not blendvb on SSE4.1.
; It does generate the proper and/or fallback for SSE<4.1.
define <4 x i32> @blend_neon_and_legacy(<4 x i32> %x, <4 x i32> %y) {
  %mask = icmp sgt <4 x i32> %x, %y
  %mask2 = sext <4 x i1> %mask to <4 x i32>
  %x_mask = and <4 x i32> %x, %mask2
  %mask_flip = xor <4 x i32> %mask2, <i32 -1, i32 -1, i32 -1, i32 -1>
  %y_mask = and <4 x i32> %y, %mask_flip
  %blended = or <4 x i32> %x_mask, %y_mask
  ret <4 x i32> %blended
}

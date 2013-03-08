
declare <4 x float> @llvm.arm.neon.vabs.v4f32(<4 x float>)

define <4 x float> @abs_f32x4(<4 x float> %x) nounwind alwaysinline {
       %tmp = call <4 x float> @llvm.arm.neon.vabs.v4f32(<4 x float> %x) 
       ret <4 x float> %tmp
}

declare <2 x float> @llvm.arm.neon.vabs.v2f32(<2 x float>)

define <2 x float> @abs_f32x2(<2 x float> %x) nounwind alwaysinline {
       %tmp = call <2 x float> @llvm.arm.neon.vabs.v2f32(<2 x float> %x) 
       ret <2 x float> %tmp
}

declare <4 x i32> @llvm.arm.neon.vabs.v4i32(<4 x i32>)

define <4 x i32> @abs_i32x4(<4 x i32> %x) nounwind alwaysinline {
       %tmp = call <4 x i32> @llvm.arm.neon.vabs.v4i32(<4 x i32> %x) 
       ret <4 x i32> %tmp
}

declare <2 x i32> @llvm.arm.neon.vabs.v2i32(<2 x i32>)

define <2 x i32> @abs_i32x2(<2 x i32> %x) nounwind alwaysinline {
       %tmp = call <2 x i32> @llvm.arm.neon.vabs.v2i32(<2 x i32> %x) 
       ret <2 x i32> %tmp
}

declare <4 x i16> @llvm.arm.neon.vabs.v4i16(<4 x i16>)

define <4 x i16> @abs_i16x4(<4 x i16> %x) nounwind alwaysinline {
       %tmp = call <4 x i16> @llvm.arm.neon.vabs.v4i16(<4 x i16> %x) 
       ret <4 x i16> %tmp
}

declare <8 x i16> @llvm.arm.neon.vabs.v8i16(<8 x i16>)

define <8 x i16> @abs_i16x8(<8 x i16> %x) nounwind alwaysinline {
       %tmp = call <8 x i16> @llvm.arm.neon.vabs.v8i16(<8 x i16> %x) 
       ret <8 x i16> %tmp
}

declare <8 x i8> @llvm.arm.neon.vabs.v8i8(<8 x i8>)

define <8 x i8> @abs_i8x8(<8 x i8> %x) nounwind alwaysinline {
       %tmp = call <8 x i8> @llvm.arm.neon.vabs.v8i8(<8 x i8> %x) 
       ret <8 x i8> %tmp
}

declare <16 x i8> @llvm.arm.neon.vabs.v16i8(<16 x i8>)

define <16 x i8> @abs_i8x16(<16 x i8> %x) nounwind alwaysinline {
       %tmp = call <16 x i8> @llvm.arm.neon.vabs.v16i8(<16 x i8> %x) 
       ret <16 x i8> %tmp
}

declare <4 x float> @llvm.sqrt.v4f32(<4 x float>);
declare <2 x double> @llvm.sqrt.v2f64(<2 x double>);

define <4 x float> @sqrt_f32x4(<4 x float> %x) nounwind alwaysinline {
       %tmp = call <4 x float> @llvm.sqrt.v4f32(<4 x float> %x)
       ret <4 x float> %tmp
}

define <2 x double> @sqrt_f64x2(<2 x double> %x) nounwind alwaysinline {
       %tmp = call <2 x double> @llvm.sqrt.v2f64(<2 x double> %x)
       ret <2 x double> %tmp
}

define <8 x i8> @strided_load_i8x8(i8 * %ptr, i32 %stride) nounwind alwaysinline {
       %tmp = tail call {<8 x i8>, i8 *} asm "
       vld1.8 $0[0], [$1], $3  
       vld1.8 $0[1], [$1], $3 
       vld1.8 $0[2], [$1], $3
       vld1.8 $0[3], [$1], $3
       vld1.8 $0[4], [$1], $3
       vld1.8 $0[5], [$1], $3
       vld1.8 $0[6], [$1], $3
       vld1.8 $0[7], [$1], $3
       ", "=w,=r,1,r"(i8 *%ptr, i32 %stride) nounwind
       %val = extractvalue {<8 x i8>, i8 *} %tmp, 0
       ret <8 x i8> %val
}

define void @strided_store_i8x8(i8 * %ptr, i32 %stride, <8 x i8> %val) nounwind alwaysinline {
       tail call i8 * asm sideeffect "
       vst1.8 $3[0], [$0], $2
       vst1.8 $3[1], [$0], $2 
       vst1.8 $3[2], [$0], $2
       vst1.8 $3[3], [$0], $2
       vst1.8 $3[4], [$0], $2
       vst1.8 $3[5], [$0], $2
       vst1.8 $3[6], [$0], $2
       vst1.8 $3[7], [$0], $2
       ", "=r,0,r,w,~{mem}"(i8 *%ptr, i32 %stride, <8 x i8> %val) nounwind
       ret void
}

define <16 x i8> @strided_load_i8x16(i8 * %ptr, i32 %stride) nounwind alwaysinline {
       %tmp = tail call {<16 x i8>, i8 *} asm "
       vld1.8 $0[0], [$1], $3  
       vld1.8 $0[1], [$1], $3 
       vld1.8 $0[2], [$1], $3
       vld1.8 $0[3], [$1], $3
       vld1.8 $0[4], [$1], $3
       vld1.8 $0[5], [$1], $3
       vld1.8 $0[6], [$1], $3
       vld1.8 $0[7], [$1], $3
       vld1.8 $0[8], [$1], $3  
       vld1.8 $0[9], [$1], $3 
       vld1.8 $0[10], [$1], $3
       vld1.8 $0[11], [$1], $3
       vld1.8 $0[12], [$1], $3
       vld1.8 $0[13], [$1], $3
       vld1.8 $0[14], [$1], $3
       vld1.8 $0[15], [$1], $3
       ", "=w,=r,1,r"(i8 *%ptr, i32 %stride) nounwind
       %val = extractvalue {<16 x i8>, i8 *} %tmp, 0
       ret <16 x i8> %val
}

define void @strided_store_i8x16(i8 * %ptr, i32 %stride, <16 x i8> %val) nounwind alwaysinline {
       tail call i8 * asm sideeffect "
       vst1.8 $3[0], [$0], $2
       vst1.8 $3[1], [$0], $2 
       vst1.8 $3[2], [$0], $2
       vst1.8 $3[3], [$0], $2
       vst1.8 $3[4], [$0], $2
       vst1.8 $3[5], [$0], $2
       vst1.8 $3[6], [$0], $2
       vst1.8 $3[7], [$0], $2
       vst1.8 $3[8], [$0], $2
       vst1.8 $3[9], [$0], $2 
       vst1.8 $3[10], [$0], $2
       vst1.8 $3[11], [$0], $2
       vst1.8 $3[12], [$0], $2
       vst1.8 $3[13], [$0], $2
       vst1.8 $3[14], [$0], $2
       vst1.8 $3[15], [$0], $2
       ", "=r,0,r,w,~{mem}"(i8 *%ptr, i32 %stride, <16 x i8> %val) nounwind
       ret void
}
 
define <4 x i16> @strided_load_i16x4(i16 * %ptr, i32 %stride) nounwind alwaysinline {
       %tmp = tail call {<4 x i16>, i16 *} asm "
       vld1.16 $0[0], [$1], $3  
       vld1.16 $0[1], [$1], $3 
       vld1.16 $0[2], [$1], $3
       vld1.16 $0[3], [$1], $3
       ", "=w,=r,1,r"(i16 *%ptr, i32 %stride) nounwind
       %val = extractvalue {<4 x i16>, i16 *} %tmp, 0
       ret <4 x i16> %val
}

define void @strided_store_i16x4(i16 * %ptr, i32 %stride, <4 x i16> %val) nounwind alwaysinline {
       tail call i16 * asm sideeffect "
       vst1.16 $3[0], [$0], $2
       vst1.16 $3[1], [$0], $2 
       vst1.16 $3[2], [$0], $2
       vst1.16 $3[3], [$0], $2
       ", "=r,0,r,w,~{mem}"(i16 *%ptr, i32 %stride, <4 x i16> %val) nounwind
       ret void
}

define <8 x i16> @strided_load_i16x8(i16 * %ptr, i32 %stride) nounwind alwaysinline {
       %tmp = tail call {<8 x i16>, i16 *} asm "
       vld1.16 $0[0], [$1], $3  
       vld1.16 $0[1], [$1], $3 
       vld1.16 $0[2], [$1], $3
       vld1.16 $0[3], [$1], $3
       vld1.16 $0[4], [$1], $3
       vld1.16 $0[5], [$1], $3
       vld1.16 $0[6], [$1], $3
       vld1.16 $0[7], [$1], $3
       ", "=w,=r,1,r"(i16 *%ptr, i32 %stride) nounwind
       %val = extractvalue {<8 x i16>, i16 *} %tmp, 0
       ret <8 x i16> %val
}

define void @strided_store_i16x8(i16 * %ptr, i32 %stride, <8 x i16> %val) nounwind alwaysinline {
       tail call i16 * asm sideeffect "
       vst1.16 $3[0], [$0], $2
       vst1.16 $3[1], [$0], $2 
       vst1.16 $3[2], [$0], $2
       vst1.16 $3[3], [$0], $2
       vst1.16 $3[4], [$0], $2
       vst1.16 $3[5], [$0], $2
       vst1.16 $3[6], [$0], $2
       vst1.16 $3[7], [$0], $2
       ", "=r,0,r,w,~{mem}"(i16 *%ptr, i32 %stride, <8 x i16> %val) nounwind
       ret void
}

define <4 x i32> @strided_load_i32x4(i32 * %ptr, i32 %stride) nounwind alwaysinline {
       %tmp = tail call {<4 x i32>, i32 *} asm "
       vld1.32 $0[0], [$1], $3  
       vld1.32 $0[1], [$1], $3 
       vld1.32 $0[2], [$1], $3
       vld1.32 $0[3], [$1], $3
       ", "=w,=r,1,r"(i32 *%ptr, i32 %stride) nounwind
       %val = extractvalue {<4 x i32>, i32 *} %tmp, 0
       ret <4 x i32> %val
}

define void @strided_store_i32x4(i32 * %ptr, i32 %stride, <4 x i32> %val) nounwind alwaysinline {
       tail call i32 * asm sideeffect "
       vst1.32 $3[0], [$0], $2
       vst1.32 $3[1], [$0], $2 
       vst1.32 $3[2], [$0], $2
       vst1.32 $3[3], [$0], $2
       ", "=r,0,r,w,~{mem}"(i32 *%ptr, i32 %stride, <4 x i32> %val) nounwind
       ret void
}

define <4 x float> @strided_load_f32x4(float * %ptr, float %stride) nounwind alwaysinline {
       %tmp = tail call {<4 x float>, float *} asm "
       vld1.32 $0[0], [$1], $3  
       vld1.32 $0[1], [$1], $3 
       vld1.32 $0[2], [$1], $3
       vld1.32 $0[3], [$1], $3
       ", "=w,=r,1,r"(float *%ptr, float %stride) nounwind
       %val = extractvalue {<4 x float>, float *} %tmp, 0
       ret <4 x float> %val
}

define void @strided_store_f32x4(float * %ptr, float %stride, <4 x float> %val) nounwind alwaysinline {
       tail call float * asm sideeffect "
       vst1.32 $3[0], [$0], $2
       vst1.32 $3[1], [$0], $2 
       vst1.32 $3[2], [$0], $2
       vst1.32 $3[3], [$0], $2
       ", "=r,0,r,w,~{mem}"(float *%ptr, float %stride, <4 x float> %val) nounwind
       ret void
}      
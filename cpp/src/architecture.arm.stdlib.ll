
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


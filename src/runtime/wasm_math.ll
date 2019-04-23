declare float @llvm.sqrt.f32(float);
declare <4 x float> @llvm.sqrt.v4f32(<4 x float>);
declare <2 x float> @llvm.sqrt.v2f32(<2 x float>);

; fast_inverse

define weak_odr float @fast_inverse_f32(float %x) nounwind alwaysinline {
       %y = fdiv float 1.0, %x
       ret float %y
}

define weak_odr <2 x float> @fast_inverse_f32x2(<2 x float> %x) nounwind alwaysinline {
       %y = fdiv <2 x float> <float 1.0, float 1.0>, %x
       ret <2 x float> %y
}

define weak_odr <4 x float> @fast_inverse_f32x4(<4 x float> %x) nounwind alwaysinline {
       %y = fdiv <4 x float> <float 1.0, float 1.0, float 1.0, float 1.0>, %x
       ret <4 x float> %y
}

; fast_inverse_sqrt

define weak_odr float @fast_inverse_sqrt_f32(float %x) nounwind alwaysinline {
       %y = call float @llvm.sqrt.f32(float %x)
       %z = fdiv float 1.0, %y
       ret float %z
}

define weak_odr <2 x float> @fast_inverse_sqrt_f32x2(<2 x float> %x) nounwind alwaysinline {
       %y = call <2 x float> @llvm.sqrt.v2f32(<2 x float> %x)
       %z = fdiv <2 x float> <float 1.0, float 1.0>, %y
       ret <2 x float> %z
}

define weak_odr <4 x float> @fast_inverse_sqrt_f32x4(<4 x float> %x) nounwind alwaysinline {
       %y = call <4 x float> @llvm.sqrt.v4f32(<4 x float> %x)
       %z = fdiv <4 x float> <float 1.0, float 1.0, float 1.0, float 1.0>, %y
       ret <4 x float> %z
}


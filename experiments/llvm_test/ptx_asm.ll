module asm "
.func (.reg .f32 %ret0) ex2f (.reg .f32 %arg0) // @ex2f
{
	.reg .f32 %f<1>;
// BB#0:
	mov.f32	%f0, %arg0;
	ex2.approx.f32	%ret0, %f0;
	//w;
	ret;
}
"

define ptx_kernel void @f(float %in, float* %out) {
  ;%1 = fdiv float %in, 4.0
  ;%1 = call float @ex2f(float %in)
  %1 = call float asm "ex2.approx.f32 $0", "=r,r"(float %in)
  store float %1, float* %out
  ret void
}

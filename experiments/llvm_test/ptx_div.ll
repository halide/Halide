define ptx_kernel void @f(float %in, float* %out) {
  %1 = fdiv float %in, 4.0
  store float %1, float* %out
  ret void
}

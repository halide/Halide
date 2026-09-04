#pragma once
// Compatibility shims to build LibBi 1.4.5 against CUDA 13 / CCCL Thrust.
// std::unary_function/binary_function are still provided by libstdc++, so no shim there.
#ifdef __CUDACC__
#define LIBBI_HD __host__ __device__
#else
#define LIBBI_HD
#endif
namespace thrust {
// thrust::identity was removed in CCCL 2.x.
template<class T = void> struct identity {
  typedef T argument_type; typedef T result_type;
  LIBBI_HD const T& operator()(const T& x) const { return x; } };
}

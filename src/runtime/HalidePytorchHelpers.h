#ifndef HL_PYTORCH_WRAPPER_H
#define HL_PYTORCH_WRAPPER_H

#include <exception>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

#include <torch/extension.h>

#include <HalideBuffer.h>

#ifdef HL_PT_CUDA
#include <HalideRuntimeCuda.h>
#include <cuda.h>
#include <cuda_runtime.h>
#endif

#define WEAK __attribute__((weak))

#define HLPT_CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x " must be contiguous")
#define HLPT_CHECK_CUDA(x) AT_ASSERTM(x.type().is_cuda(), #x " must be a CUDA tensor")
#define HLPT_CHECK_DEVICE(x, dev) AT_ASSERTM(x.is_cuda() && x.get_device() == dev, #x " must be a CUDA tensor")


namespace Halide {
namespace Pytorch {

using Halide::Runtime::Buffer;

inline std::vector<int> getDims(const at::Tensor tensor) {
  int ndims = tensor.ndimension();
  std::vector<int> dims(ndims, 0);
  // PyTorch dim order is reverse of Halide
  for(int dim = 0; dim < ndims; ++dim) {
    dims[dim] = tensor.size(ndims-1-dim);
  }
  return dims;
}


template<class scalar_t>
inline void check_type(at::Tensor &tensor) {
  AT_ERROR("Scalar type ", tensor.scalar_type(), " not handled by Halide's Pytorch wrapper");
}


#define HL_PT_DEFINE_TYPECHECK(ctype,ttype,_3) \
  template<> \
  inline void check_type<ctype>(at::Tensor &tensor) { \
    AT_ASSERTM(tensor.scalar_type() == at::ScalarType::ttype, "scalar type do not match"); \
  }
  AT_FORALL_SCALAR_TYPES_EXCEPT_HALF(HL_PT_DEFINE_TYPECHECK)
#undef HL_PT_DEFINE_TYPECHECK


// TODO(mgharbi): deal with const data in the buffer
template<class scalar_t>
inline Buffer<scalar_t> wrap(at::Tensor &tensor) {
  check_type<scalar_t>(tensor);
  std::vector<int> dims = getDims(tensor);
  scalar_t* pData  = tensor.data<scalar_t>();
  Buffer<scalar_t> buffer;

  // TODO(mgharbi): force Halide to put input/output on GPU?
  if(tensor.is_cuda()) {
    #ifdef HL_PT_CUDA
    buffer = Buffer<scalar_t>(dims);
    const halide_device_interface_t* cuda_interface = halide_cuda_device_interface();
    int err = buffer.device_wrap_native(cuda_interface, (uint64_t)pData);
    AT_ASSERTM(err==0,  "halide_device_wrap failed");
    buffer.set_device_dirty();
    #else
    AT_ERROR("Trying to wrap a CUDA tensor, but HL_PT_CUDA was not defined: cuda is not available");
    #endif
  } else {
    buffer = Buffer<scalar_t>(pData, dims);
  }

  return buffer;
}

} // namespace Pytorch
} // namespace Halide

#endif  // HL_PYTORCH_WRAPPER_H

#ifndef HL_PYTORCH_WRAPPER_H
#define HL_PYTORCH_WRAPPER_H

/** \file
 * Set of utility functions to wrap PyTorch tensors into Halide buffers,
 * making sure the data in on the correct device (CPU/GPU). This header
 * is included in each generated op by the PyTorch CodeGen.
 */

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "HalideBuffer.h"

// Forward declare the cuda_device_interface, for tensor wrapper.
const halide_device_interface_t *halide_cuda_device_interface();

#define HLPT_CHECK_CONTIGUOUS(x) AT_ASSERTM(x.is_contiguous(), #x " must be contiguous")
#define HLPT_CHECK_CUDA(x) AT_ASSERTM(x.type().is_cuda(), #x " must be a CUDA tensor")
#define HLPT_CHECK_DEVICE(x, dev) AT_ASSERTM(x.is_cuda() && x.get_device() == dev, #x " must be a CUDA tensor")

namespace Halide {
namespace PyTorch {

using Halide::Runtime::Buffer;

inline std::vector<int> get_dims(const at::Tensor tensor) {
    int ndims = tensor.ndimension();
    std::vector<int> dims(ndims, 0);
    // PyTorch dim order is reverse of Halide
    for (int dim = 0; dim < ndims; ++dim) {
        dims[dim] = tensor.size(ndims - 1 - dim);
    }
    return dims;
}

template<class scalar_t>
inline void check_type(at::Tensor &tensor) {
    AT_ERROR("Scalar type ", tensor.scalar_type(), " not handled by Halide's PyTorch wrapper");
}

// TODO: if PyTorch exposes any variable with the API version,
// I haven't found it in source or documentation; for now, we'll sniff
// this macro's existence to infer that we are building with v1.3+ (vs 1.2)
#ifdef AT_FORALL_SCALAR_TYPES_WITH_COMPLEX_AND_QINTS
#define HL_PYTORCH_API_VERSION 13
#else
#define HL_PYTORCH_API_VERSION 12
#endif

#if HL_PYTORCH_API_VERSION >= 13

// PyTorch 1.3+
#define HL_PT_DEFINE_TYPECHECK(ctype, ttype)                                                   \
    template<>                                                                                 \
    inline void check_type<ctype>(at::Tensor & tensor) {                                       \
        AT_ASSERTM(tensor.scalar_type() == at::ScalarType::ttype, "scalar type do not match"); \
    }

AT_FORALL_SCALAR_TYPES_WITH_COMPLEX_AND_QINTS(HL_PT_DEFINE_TYPECHECK);

#undef HL_PT_DEFINE_TYPECHECK

#else  // HL_PYTORCH_API_VERSION < 13

// PyTorch 1.2

#define HL_PT_DEFINE_TYPECHECK(ctype, ttype, _3)                                               \
    template<>                                                                                 \
    inline void check_type<ctype>(at::Tensor & tensor) {                                       \
        AT_ASSERTM(tensor.scalar_type() == at::ScalarType::ttype, "scalar type do not match"); \
    }

AT_FORALL_SCALAR_TYPES_WITH_COMPLEX(HL_PT_DEFINE_TYPECHECK);

#undef HL_PT_DEFINE_TYPECHECK

#endif  // HL_PYTORCH_API_VERSION check

template<class scalar_t>
inline Buffer<scalar_t> wrap(at::Tensor &tensor) {
    check_type<scalar_t>(tensor);
    std::vector<int> dims = get_dims(tensor);
#if HL_PYTORCH_API_VERSION >= 13
    scalar_t *pData = tensor.data_ptr<scalar_t>();
#else
    scalar_t *pData = tensor.data<scalar_t>();
#endif
    return Buffer<scalar_t>(pData, dims);
}

template<class scalar_t>
inline Buffer<scalar_t> wrap_cuda(at::Tensor &tensor) {
    check_type<scalar_t>(tensor);
    std::vector<int> dims = get_dims(tensor);
#if HL_PYTORCH_API_VERSION >= 13
    scalar_t *pData = tensor.data_ptr<scalar_t>();
#else
    scalar_t *pData = tensor.data<scalar_t>();
#endif
    AT_ASSERTM(tensor.is_cuda(), "expected input tensor to be on a CUDA device.");

    Buffer<scalar_t> buffer(dims);

    const halide_device_interface_t *cuda_interface = halide_cuda_device_interface();
    int err = buffer.device_wrap_native(cuda_interface, (uint64_t)pData);
    AT_ASSERTM(err == 0, "(CUDA) halide_device_wrap failed");

    buffer.set_device_dirty();

    return buffer;
}

}  // namespace PyTorch
}  // namespace Halide

#endif  // HL_PYTORCH_WRAPPER_H

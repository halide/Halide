#ifndef HALIDE_PYTHON_BINDINGS_PYBUFFER_H
#define HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_buffer(py::module &m);

Type format_descriptor_to_type(const std::string &fd);

py::object buffer_getitem_operator(Buffer<> &buf, const std::vector<int> &pos);

template<typename T = void,
         int Dims = AnyDims,
         int InClassDimStorage = (Dims == AnyDims ? 4 : std::max(Dims, 1))>
Halide::Runtime::Buffer<T, Dims, InClassDimStorage> pybufferinfo_to_halidebuffer(const py::buffer_info &info, bool reverse_axes) {
    const Type t = format_descriptor_to_type(info.format);
    halide_dimension_t *dims = (halide_dimension_t *)alloca(info.ndim * sizeof(halide_dimension_t));
    _halide_user_assert(dims);
    for (int i = 0; i < info.ndim; i++) {
        if (INT_MAX < info.shape[i] || INT_MAX < (info.strides[i] / t.bytes())) {
            throw py::value_error("Out of range dimensions in buffer conversion.");
        }
        // Halide's default indexing convention is col-major (the most rapidly varying index comes first);
        // Numpy's default is row-major (most rapidly varying comes last).
        // We usually want to reverse the order so that most-varying comes first.
        const int dst_axis = reverse_axes ? (info.ndim - i - 1) : i;
        dims[dst_axis] = {0, (int32_t)info.shape[i], (int32_t)(info.strides[i] / t.bytes())};
    }
    return Halide::Runtime::Buffer<T, Dims, InClassDimStorage>(t, info.ptr, (int)info.ndim, dims);
}

template<typename T = void,
         int Dims = AnyDims,
         int InClassDimStorage = (Dims == AnyDims ? 4 : std::max(Dims, 1))>
Halide::Runtime::Buffer<T, Dims, InClassDimStorage> pybuffer_to_halidebuffer(const py::buffer &pyb, bool writable, bool reverse_axes) {
    return pybufferinfo_to_halidebuffer(pyb.request(writable), reverse_axes);
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBUFFER_H

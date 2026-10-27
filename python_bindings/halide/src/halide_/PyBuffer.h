#ifndef HALIDE_PYTHON_BINDINGS_PYBUFFER_H
#define HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#include "PyBufferProtocol.h"
#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_buffer(py::module_ &m);

py::object buffer_getitem_operator(Buffer<> &buf, const std::vector<int> &pos);

template<typename T = void,
         int Dims = AnyDims,
         int InClassDimStorage = (Dims == AnyDims ? 4 : std::max(Dims, 1))>
Halide::Runtime::Buffer<T, Dims, InClassDimStorage> pyarray_to_halidebuffer(const PythonArray &array, bool reverse_axes) {
    const Type t = python_dtype_to_halide_type(array.dtype());
    halide_dimension_t *dims = (halide_dimension_t *)alloca(array.ndim() * sizeof(halide_dimension_t));
    _halide_user_assert(dims);
    python_array_dimensions(array, dims, reverse_axes);
    return Halide::Runtime::Buffer<T, Dims, InClassDimStorage>(t.to_abi(), const_cast<void *>(array.data()), (int)array.ndim(), dims);
}

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBUFFER_H

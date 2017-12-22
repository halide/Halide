#ifndef HALIDE_PYTHON_BINDINGS_PYBUFFER_H
#define HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_buffer();
py::object buffer_to_python_object(const Buffer<> &);
Buffer<> python_object_to_buffer(py::object);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#ifndef HALIDE_PYTHON_BINDINGS_PYERROR_H
#define HALIDE_PYTHON_BINDINGS_PYERROR_H

namespace pybind11 {
class module;
}  // namespace pybind11

namespace Halide {
namespace PythonBindings {

void define_error(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYERROR_H

#ifndef HALIDE_PYTHON_BINDINGS_PYLAMBDA_H
#define HALIDE_PYTHON_BINDINGS_PYLAMBDA_H

namespace pybind11 {
class module;
}  // namespace pybind11

namespace Halide {
namespace PythonBindings {

void define_lambda(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYLAMBDA_H

#ifndef HALIDE_PYTHON_BINDINGS_PYEXPR_H
#define HALIDE_PYTHON_BINDINGS_PYEXPR_H

namespace pybind11 {
class module;
}  // namespace pybind11

namespace Halide {
namespace PythonBindings {

void define_expr(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYEXPR_H

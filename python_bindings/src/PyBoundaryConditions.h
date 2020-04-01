#ifndef HALIDE_PYTHON_BINDINGS_PYBOUNDARYCONDITIONS_H
#define HALIDE_PYTHON_BINDINGS_PYBOUNDARYCONDITIONS_H

namespace pybind11 {
class module;
}  // namespace pybind11

namespace Halide {
namespace PythonBindings {

void define_boundary_conditions(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBOUNDARYCONDITIONS_H

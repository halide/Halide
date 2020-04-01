#ifndef HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H
#define HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H

namespace pybind11 {
class module;
}  // namespace pybind11

namespace Halide {
namespace PythonBindings {

void define_machine_params(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYMACHINEPARAMS_H

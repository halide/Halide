#ifndef HALIDE_PYTHON_BINDINGS_PYPARAMETER_H
#define HALIDE_PYTHON_BINDINGS_PYPARAMETER_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_parameter(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYPARAMETER_H

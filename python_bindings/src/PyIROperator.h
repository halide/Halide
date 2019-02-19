#ifndef HALIDE_PYTHON_BINDINGS_PYIROPERATOR_H
#define HALIDE_PYTHON_BINDINGS_PYIROPERATOR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_operators(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYIROPERATOR_H

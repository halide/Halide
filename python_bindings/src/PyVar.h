#ifndef HALIDE_PYTHON_BINDINGS_PYVAR_H
#define HALIDE_PYTHON_BINDINGS_PYVAR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_var(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYVAR_H

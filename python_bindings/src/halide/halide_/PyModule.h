#ifndef HALIDE_PYTHON_BINDINGS_PYMODULE_H
#define HALIDE_PYTHON_BINDINGS_PYMODULE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_module(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYMODULE_H

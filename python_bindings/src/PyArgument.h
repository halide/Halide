#ifndef HALIDE_PYTHON_BINDINGS_PYARGUMENT_H
#define HALIDE_PYTHON_BINDINGS_PYARGUMENT_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_argument(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYARGUMENT_H

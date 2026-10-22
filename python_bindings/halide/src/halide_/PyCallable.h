#ifndef HALIDE_PYTHON_BINDINGS_PYCALLABLE_H
#define HALIDE_PYTHON_BINDINGS_PYCALLABLE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_callable(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYCALLABLE_H

#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_func(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_H

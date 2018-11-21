#ifndef HALIDE_PYTHON_BINDINGS_PYLAMBDA_H
#define HALIDE_PYTHON_BINDINGS_PYLAMBDA_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_lambda(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYLAMBDA_H

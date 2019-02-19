#ifndef HALIDE_PYTHON_BINDINGS_PYOUTPUTS_H
#define HALIDE_PYTHON_BINDINGS_PYOUTPUTS_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_outputs(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYOUTPUTS_H

#ifndef HALIDE_PYTHON_BINDINGS_PYLOOPLEVEL_H
#define HALIDE_PYTHON_BINDINGS_PYLOOPLEVEL_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_loop_level(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYLOOPLEVEL_H

#ifndef HALIDE_PYTHON_BINDINGS_PYIMAGEPARAM_H
#define HALIDE_PYTHON_BINDINGS_PYIMAGEPARAM_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_image_param(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYIMAGEPARAM_H

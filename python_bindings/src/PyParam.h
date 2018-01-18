#ifndef HALIDE_PYTHON_BINDINGS_PYPARAM_H
#define HALIDE_PYTHON_BINDINGS_PYPARAM_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_param(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYPARAM_H

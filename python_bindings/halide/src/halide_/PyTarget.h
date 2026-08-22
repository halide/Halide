#ifndef HALIDE_PYTHON_BINDINGS_PYTARGET_H
#define HALIDE_PYTHON_BINDINGS_PYTARGET_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_target(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYTARGET_H

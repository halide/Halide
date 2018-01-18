#ifndef HALIDE_PYTHON_BINDINGS_PYRDOM_H
#define HALIDE_PYTHON_BINDINGS_PYRDOM_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_rdom(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYRDOM_H

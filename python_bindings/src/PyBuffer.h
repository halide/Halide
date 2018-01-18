#ifndef HALIDE_PYTHON_BINDINGS_PYBUFFER_H
#define HALIDE_PYTHON_BINDINGS_PYBUFFER_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_buffer(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYBUFFER_H

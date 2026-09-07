#ifndef HALIDE_PYTHON_BINDINGS_PYENUMS_H
#define HALIDE_PYTHON_BINDINGS_PYENUMS_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_enums(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYENUMS_H

#ifndef HALIDE_PYTHON_BINDINGS_PYGENERATOR_H
#define HALIDE_PYTHON_BINDINGS_PYGENERATOR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_generator(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYGENERATOR_H

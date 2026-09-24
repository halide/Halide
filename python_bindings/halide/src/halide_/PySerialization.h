#ifndef HALIDE_PYTHON_BINDINGS_PYSERIALIZATION_H
#define HALIDE_PYTHON_BINDINGS_PYSERIALIZATION_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_serialization(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYSERIALIZATION_H

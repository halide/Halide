#ifndef HALIDE_PYTHON_BINDINGS_PYTYPE_H
#define HALIDE_PYTHON_BINDINGS_PYTYPE_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_type(py::module &m);

std::string halide_type_to_string(const Type &type);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYTYPE_H

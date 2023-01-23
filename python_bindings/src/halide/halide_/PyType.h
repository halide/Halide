#ifndef HALIDE_PYTHON_BINDINGS_PYTYPE_H
#define HALIDE_PYTHON_BINDINGS_PYTYPE_H

#include "PyHalide.h"

// This is an expedient: a unique Handle type that we use as a placeholder
// in Python generators. It should never be visible to the end user.
struct UnspecifiedType {};
HALIDE_DECLARE_EXTERN_STRUCT_TYPE(UnspecifiedType);

namespace Halide {
namespace PythonBindings {

void define_type(py::module &m);

std::string halide_type_to_string(const Type &type);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYTYPE_H

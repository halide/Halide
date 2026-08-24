#ifndef HALIDE_PYTHON_BINDINGS_PYFUNC_VARORRVAR_H
#define HALIDE_PYTHON_BINDINGS_PYFUNC_VARORRVAR_H

#include "PyHalide.h"

namespace Halide {
namespace PythonBindings {

void define_var_or_rvar(py::module &m);

}  // namespace PythonBindings
}  // namespace Halide

#endif  // HALIDE_PYTHON_BINDINGS_PYFUNC_VARORRVAR_H
